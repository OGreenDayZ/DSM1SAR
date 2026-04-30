#include "worker.h"

#include "debug/debug.h"
#include "internal.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "network/pager/pager_msg.h"
#include "pager/pager.h"
#include "util/concurrent/safe_list.h"

#include <bits/pthreadtypes.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>

dsm_worker_ctx *dsm_wkr_ctx = NULL;

int dsm_wkr_ctx_init() {
    LOG_INFO("Initialisation du contexte du worker ...");

    if (dsm_wkr_ctx) {
        LOG_ERROR("dsm_wkr_ctx déjà initialisé ?!");
        return -1;
    }

    dsm_wkr_ctx = malloc(sizeof(dsm_worker_ctx));
    if (!dsm_wkr_ctx) {
        LOG_ERROR("malloc : wkr_ctx");
        return -1;
    }

    if (userfaultfd_init(&dsm_wkr_ctx->uffd, &dsm_wkr_ctx->range_start) < 0) {
        LOG_ERROR("userfaultfd_init");
        return -1;
    }

    if (dsm_safe_map_init(&dsm_wkr_ctx->managerID_to_nodeID, DSM_WKR_NODE_HASH_BITS) <
        0) {
        return -1;
    }

    if (dsm_safe_map_init(&dsm_wkr_ctx->requests_reg, DSM_WKR_PAGE_HASH_BITS) < 0) {
        LOG_ERROR("[dsm_wkr_ctx_init] dsm_safe_map_init dsm_wkr_ctx->requests_reg");
        return -1;
    }

    if (dsm_safe_list_init(&dsm_wkr_ctx->locks_reg) < 0) {
        LOG_ERROR("[dsm_wkr_ctx_init] dsm_safe_list_init dsm_wkr_ctx->locks_reg");
        return -1;
    }

    pthread_mutex_init(&dsm_wkr_ctx->sync.barr.lock, NULL);
    pthread_cond_init(&dsm_wkr_ctx->sync.barr.wait_cond, NULL);

    dsm_wkr_ctx->sync.barr.targ_mgr_id = 0; // Par défaut, ca sera toujours le ManagerID=0

    dsm_wkr_ctx->sync.barr.curr_count = 0;
    dsm_wkr_ctx->sync.barr.targ_count = 0;

    pthread_mutex_init(&dsm_wkr_ctx->sync.fina.lock, NULL);
    pthread_cond_init(&dsm_wkr_ctx->sync.fina.wait_cond, NULL);

    dsm_wkr_ctx->sync.fina.curr_count = 0;
    dsm_wkr_ctx->sync.fina.targ_count = 0;

    dsm_wkr_ctx->page_counter = 0;
    return 0;
}

void dsm_wkr_ctx_cleanup();

long addr_to_page_id(uint64_t addr) {
    if (addr < dsm_wkr_ctx->range_start ||
        addr >= dsm_wkr_ctx->range_start + EFFECTIVE_PAGESIZE * EFFECTIVE_NB_PAGES) {
        return -1;
    }

    return (addr - dsm_wkr_ctx->range_start) / EFFECTIVE_PAGESIZE;
}

long page_id_to_addr(long page_id) {
    if (page_id < 0 || page_id >= EFFECTIVE_NB_PAGES) {
        return -1;
    }
    return dsm_wkr_ctx->range_start + page_id * EFFECTIVE_PAGESIZE;
}
static unsigned int hash_page_to_mgr(unsigned long page_id) {
    return page_id % dsm_ctx->config.net.system.nb_managers;
}

static unsigned int get_mgr_id_for_page(unsigned long page_id) {
    return hash_page_to_mgr(page_id);
}

static bool get_mgr_convert_node(unsigned long page_id,
                                 dsm_mgr_to_node_content **out_content) {
    unsigned int mgr_id = get_mgr_id_for_page(page_id);
    bool found = false;

    dsm_mgr_to_node_content *content;
    dsm_hash_safe_get(&dsm_wkr_ctx->managerID_to_nodeID, content, node, mgr_id) {
        if (content->manager_id == mgr_id) {
            *out_content = content;
            found = true;
        }
    }
    dsm_hash_safe_get_end(&dsm_wkr_ctx->managerID_to_nodeID, mgr_id);
    if (!found) {
        LOG_ERROR("Manager pour page %lu (mgr_id %u) non trouvé", page_id, mgr_id);
    }
    return found;
}

int get_mgr_node_id_for_page(unsigned long page_id) {
    dsm_mgr_to_node_content *content;
    if (!get_mgr_convert_node(page_id, &content)) {
        return -1;
    }
    return content->node_id;
}

int get_mgr_node_socket_for_page(unsigned long page_id) {
    dsm_mgr_to_node_content *content;
    if (!get_mgr_convert_node(page_id, &content)) {
        return -1;
    }
    return content->to_node_content->tcp_sock;
}

pthread_mutex_t get_mgr_send_lock_for_page(unsigned long page_id) {
    int manager_id = get_mgr_id_for_page(page_id);
    dsm_node_content *entry = NULL, *obj;
    dsm_hash_safe_get(&dsm_ctx->nodes_reg, obj, node, manager_id) {
        if (obj->id == manager_id) {
            entry = obj;
            break;
        }
    }
    dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, manager_id);
    return entry->send_lock;
}

// TODO - majda : revoir, est ce que j'ajoute tout le temps la requete, ou si il existe
// une requete dont started = 1, alors ne pas ajouter la requete
// Il faut supprimer la requete et des qu'elle est fini donc qu'on recoit un ack pour elle
// Ne pas ajouter la requete tout le temps !!!
dsm_requests_reg_list_entry *add_request(unsigned long page_id, page_mode_req mode) {
    dsm_requests_reg_entry *entry_map = NULL, *entry_parcours = NULL;
    dsm_requests_reg_list_entry *to_be_added =
        malloc(sizeof(dsm_requests_reg_list_entry));

    dsm_hash_safe_get(&dsm_wkr_ctx->requests_reg, entry_parcours, node, page_id) {
        entry_map = entry_parcours;
    }
    dsm_hash_safe_get_end(&dsm_wkr_ctx->requests_reg, page_id);

    to_be_added->mode_req = mode;
    to_be_added->started = 0;
    to_be_added->dont_do = 0;

    dsm_safe_list_add_tail(&entry_map->list_req, &to_be_added->node);
    printf("[add_request] Page %lu: Requête type %d ajoutée à la liste (addr: %p)\n",
           page_id, mode, (void *)to_be_added);

    return to_be_added;
}

// TODO - majda : a tester avec manager
// Est ce que le supprime les demandes de lock non faite ?
// Et pour unlock ??
int delete_satisfied_request(unsigned long page_id) {
    dsm_requests_reg_entry *entry_map = NULL, *entry_parcours = NULL;
    dsm_requests_reg_list_entry *req_pos, *tmp;
    int deleted_count = 0;

    dsm_hash_safe_get(&dsm_wkr_ctx->requests_reg, entry_parcours, node, page_id) {
        entry_map = entry_parcours;
    }
    dsm_hash_safe_get_end(&dsm_wkr_ctx->requests_reg, page_id);

    if (!entry_map)
        return 0;

    // On verrouille la liste
    dsm_safe_list_for_each_entry_safe(&entry_map->list_req, req_pos, tmp, node) {

        if (req_pos->started == 1 || req_pos->dont_do == 1) {
            printf("[delete_batch] Nettoyage req type %d (addr: %p, started: %d, "
                   "dont_do: %d)\n",
                   req_pos->mode_req, (void *)req_pos, req_pos->started,
                   req_pos->dont_do);

            dsm_safe_list_del(&entry_map->list_req, &req_pos->node);
            free(req_pos);
            deleted_count++;
        } else {
            // On tombe sur une requête qui a 'dont_do == 0' et 'started == 0'
            // Cela signifie que c'est une requête qui attend son tour pour être envoyée.
            printf(
                "[delete_batch] Arrêt : Prochaine requête active détectée (addr: %p)\n",
                (void *)req_pos);
            break;
        }
    }

    return deleted_count;
}

/**
 * @brief
 *
 * @param page_id
 * @param mode
 * @return int 0 si ca existe pas, 1 si ca existe
 */
// TODO - majda : revoir la logique
int has_higher_priority_pending(unsigned long page_id, page_mode_req mode) {
    dsm_requests_reg_entry *entry_map = NULL, *entry_parcours = NULL;
    int higher_exist = 0;
    dsm_requests_reg_list_entry *req_pos;

    dsm_hash_safe_get(&dsm_wkr_ctx->requests_reg, entry_parcours, node, page_id) {
        entry_map = entry_parcours;
    }
    dsm_hash_safe_get_end(&dsm_wkr_ctx->requests_reg, page_id);

    if (!entry_map)
        return 0;

    dsm_safe_list_for_each_entry(&entry_map->list_req, req_pos, node) {
        // Log de diagnostic pour chaque élément de la liste
        printf(
            "[priority_check] Page %lu: Analyse req en liste (Mode: %d, Started: %d)\n",
            page_id, req_pos->mode_req, req_pos->started);

        if (mode == LOCK_REQ) {
            printf("[priority_check] faut toujours envoyé les locks\n");
            higher_exist = 0;
            dsm_safe_list_unlock(&entry_map->list_req);
            break;
        }
        // Si on a READ non commencé et un WRITE
        if (mode > req_pos->mode_req && req_pos->started == 0 && req_pos->dont_do == 0) {
            printf("[priority_check] Si ya une requete inferieur pas commencé on "
                   "l'annule\n");
            req_pos->dont_do = 1;
            higher_exist = 0;
            dsm_safe_list_unlock(&entry_map->list_req);
            break;
        }

        if (mode <= req_pos->mode_req ||
            (mode <= req_pos->mode_req && req_pos->started == 1)) {
            higher_exist = 1;
            printf("[priority_check] -> BLOQUÉ : Une requête identique ou plus forte est "
                   "déjà en vol.\n");
            dsm_safe_list_unlock(&entry_map->list_req);
            break;
        }
    }

    dsm_safe_list_for_each_entry(&entry_map->list_req, req_pos, node) {
        printf("[priority_check] PRINT_LIST - Page %lu: Analyse req en liste (Mode: %d, "
               "Started: %d, Dont_do: %d)\n",
               page_id, req_pos->mode_req, req_pos->started, req_pos->dont_do);
    }

    if (higher_exist == 0) {
        printf("[priority_check] -> OK : Aucune requête couvrante en cours. Envoi "
               "nécessaire.\n");
    }

    return higher_exist;
}

static int treat_uffd_pagefault(struct uffd_msg *fault_msg) {
    __u64 addr = fault_msg->arg.pagefault.address;
    int page_id = addr_to_page_id(addr);
    int flags = fault_msg->arg.pagefault.flags;
    int fd_sock_mgr = get_mgr_node_socket_for_page(page_id);
    pthread_mutex_t send_lock = get_mgr_send_lock_for_page(page_id);
    dsm_requests_reg_list_entry *req_entry;
    int ret = 0;

    page_mode_req mode = (flags & (UFFD_PAGEFAULT_FLAG_WRITE | UFFD_PAGEFAULT_FLAG_WP))
                             ? WRITE_REQ
                             : READ_REQ;

    printf("[uffd_fault] Thread %lu tente d'accéder à page %d en %s\n",
           (unsigned long)pthread_self(), page_id,
           (mode == WRITE_REQ ? "WRITE" : "READ"));

    pthread_mutex_lock(&send_lock);
    printf("[uffd_fault] Lock acquis pour page %d\n", page_id);

    if (has_higher_priority_pending(page_id, mode) == 1) {
        printf("[uffd_fault] Décision: PASSAGER. Un autre thread gère déjà la requête "
               "réseau.\n");
        req_entry = add_request(page_id, mode);
        req_entry->dont_do = 1;
        req_entry->started = 0; // Ce thread ne fait rien sur le réseau
    } else {
        printf("[uffd_fault] Décision: MESSAGER. Ce thread envoie la requête %d au "
               "manager.\n",
               mode);
        req_entry = add_request(page_id, mode);
        req_entry->dont_do = 0;
        req_entry->started = 1; // On marque comme lancé avant d'envoyer
        ret = send_wkr_req(page_id, mode, fd_sock_mgr);
    }

    printf("[uffd_fault] Relâchement du lock pour page %d\n", page_id);
    pthread_mutex_unlock(&send_lock);
    return ret;
}

void dispatch_call_uffd(void *arg) {
    dsm_msg_uffd_thread_ctx *msg_ctx = (dsm_msg_uffd_thread_ctx *)arg;
    struct uffd_msg *fault_msg = &msg_ctx->fault_msg;

    switch (fault_msg->event) {
        case UFFD_EVENT_PAGEFAULT:
            printf("[dispatch_call_uffd] Reception d'une pagefault\n");
            if (treat_uffd_pagefault(fault_msg) < 0) {
                LOG_ERROR("Erreur lors du traitement du pagefault");
            }
            break;

        case UFFD_EVENT_REMOVE:
            /* TODO : traiter l'invalidation de page */
            // Peut être pas necessaire a voir
            break;

        case UFFD_EVENT_UNMAP:
            /* TODO : traiter l'unmap */
            // Peut être pas necessaire a voir
            break;
    }

    free(msg_ctx);
}

/**
 * @brief Traitement d'une demande d'invalidation
 *
 * @return int
 */
static int treat_mgr_inva_req(dsm_msg_tcpss_thread_ctx *msg_ctx) {
    dsm_msg_mgr_inva_req payload;

    if (dsm_dsrlze_mgr_inva_req(msg_ctx->payload, msg_ctx->header.payload_len, &payload) <
        0) {
        LOG_ERROR("dsm_dsrlze_mgr_inva_req");
        return -1;
    }
    void *addr = (void *)page_id_to_addr(payload.page_id);
    if (madvise(addr, EFFECTIVE_PAGESIZE, MADV_DONTNEED) < 0) {
        LOG_ERROR("madvise");
        return -1;
    }
    if (payload.is_sync) {
        /**
         * Faut repondre avec msg_wkr_inva_ack
         */
        pthread_mutex_lock(msg_ctx->send_lock);
        int ret = send_wkr_inva_ack(payload.page_id, msg_ctx->sock);
        pthread_mutex_unlock(msg_ctx->send_lock);
        return ret;
    }
    return 0;
}

/**
 * @brief Traitement de la reception d'une demande de données de la part d'un manager
 *
 * @return int
 */
static int treat_mgr_data_req(dsm_msg_tcpss_thread_ctx *msg_ctx) {
    dsm_msg_mgr_data_req payload;
    if (dsm_dsrlze_mgr_data_req(msg_ctx->payload, msg_ctx->header.payload_len, &payload) <
        0) {
        LOG_ERROR("dsm_dsrlze_mgr_data_req");
        return -1;
    }

    void *zero_page = calloc(1, EFFECTIVE_PAGESIZE);
    void *addr = (void *)page_id_to_addr(payload.page_id);

    if (payload.is_invalidate) {
        /**
         * On invalide la page
         */
        if (madvise(addr, EFFECTIVE_PAGESIZE, MADV_DONTNEED) < 0) {
            LOG_ERROR("madvise");
            return -1;
        }
    }
    /**
     * envoyer dsm_msg_wkr_data_ack
     */
    pthread_mutex_lock(msg_ctx->send_lock);
    int ret = send_wkr_data_ack(payload.page_id, (uint8_t *)addr, msg_ctx->sock);
    pthread_mutex_unlock(msg_ctx->send_lock);
    free(zero_page);
    return ret;
}
/**
 * @brief Traitement de la reception des données apres une requete.
 *
 * TODO - majda : gerer LOCK et UNLOCK
 *
 * @param msg_ctx
 * @return int
 */
static int treat_mgr_req_ack(dsm_msg_tcpss_thread_ctx *msg_ctx) {
    dsm_msg_mgr_req_ack *payload;
    void *zero_page =
        calloc(1, EFFECTIVE_PAGESIZE); // alloue une plage de EFFECTIVE_PAGESIZE
                                       // contigue initialisée à 0
    dsm_locks_reg_entry *entry, *n;

    if (dsm_dsrlze_mgr_req_ack(msg_ctx->payload, msg_ctx->header.payload_len, &payload) <
        0) {
        LOG_ERROR("dsm_dsrlze_mgr_req_ack");
        return -1;
    }

    switch (payload->is_empty) {
        case EMPTY_INIT:
            if (pager_copy(
                    dsm_wkr_ctx->uffd, payload->page_id, (__u64)zero_page,
                    (payload->req_mode == WRITE_REQ || payload->req_mode == LOCK_REQ)
                        ? 0
                        : UFFDIO_COPY_MODE_WP) < 0) {
                goto error;
            }
            break;
        case NOT_EMPTY:
            if (pager_copy(
                    dsm_wkr_ctx->uffd, payload->page_id, (__u64)payload->data,
                    (payload->req_mode == WRITE_REQ || payload->req_mode == LOCK_REQ)
                        ? 0
                        : UFFDIO_COPY_MODE_WP) < 0) {
                goto error;
            }
            break;
        case PROMOTE_ONLY:
            if (pager_protect(dsm_wkr_ctx->uffd, payload->page_id, 0) < 0) {
                goto error;
            }
            break;
        default:
            break;
    }

    // TODO majda - revoir
    switch (payload->req_mode) {
        case LOCK_REQ:
            // Utilisation de locks_reg
            // Ici on a reçu une reponse a une demande de lock
            // On doit s'assurer qu'on débloque le bon thread avec locks_reg
            // locks_reg est une liste FIFO, donc on est sur que le 1er thread qui a
            // envoyé sa requete est le 1er a être réveillé
            // donc on parcours la liste a la recherche de la 1ere demande de lock associé
            // a cette page
            dsm_safe_list_for_each_entry_safe(&dsm_wkr_ctx->locks_reg, entry, n, node) {
                if (entry->page_id == payload->page_id) {
                    pthread_mutex_lock(&entry->mutex);
                    entry->locked = 1;
                    pthread_cond_signal(&entry->cond);
                    pthread_mutex_unlock(&entry->mutex);
                    dsm_safe_list_unlock(&dsm_wkr_ctx->locks_reg);
                    break;
                }
            }
            break;
        case UNLOCK_REQ:
            // pas de ca ici
        case WRITE_REQ:
            // maybe or maybe not, but i think not
            // WEEE je dois supprimer les entrés de la requests_pending_list

            break;
        case READ_REQ:
            break;
    }
    delete_satisfied_request(payload->page_id);
    free(zero_page);
    return 0;
error:
    free(zero_page);
    return -1;
}

void dispatch_call_wkr(void *arg) {
    /**
     * Contient :
     * fd_sock de qui envoie le message
     * header
     * payload
     */
    dsm_msg_tcpss_thread_ctx *msg_ctx = (dsm_msg_tcpss_thread_ctx *)arg;

    int code = msg_ctx->header.code;

    switch (code) {
        case CODE_WKR_REQ:
            // treat

        case CODE_MGR_REQ_ACK:
            // treat
            LOG_INFO("Reception par le noeud %d du code CODE_MGR_REQ_ACK et traitement.",
                     dsm_ctx->node_id);
            treat_mgr_req_ack(
                msg_ctx); /** @a TODO Ne pas oublier de < 0 check, c'est pas du void */
            break;
        case CODE_MGR_INVA_REQ:
            LOG_INFO("Reception par le noeud %d du code CODE_MGR_INVA_REQ et traitement.",
                     dsm_ctx->node_id);
            treat_mgr_inva_req(msg_ctx);
            break;
        case CODE_MGR_DATA_REQ:
            treat_mgr_data_req(msg_ctx);
            LOG_INFO("Reception par le noeud %d du code CODE_MGR_DATA_REQ et traitement.",
                     dsm_ctx->node_id);
            break;

        default:
            LOG_WARNING("Code de message TCP inconnu ou non traitable: %d", code);
    }

    free(msg_ctx->payload);
    free(msg_ctx);
}
