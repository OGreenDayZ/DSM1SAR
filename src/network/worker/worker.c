#include "worker.h"

#include <asm-generic/int-ll64.h>
#include <bits/pthreadtypes.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "debug/debug.h"
#include "internal.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "network/pager/pager_msg.h"
#include "network/worker/sync.h"
#include "pager/pager.h"
#include "util/concurrent/safe_list.h"
#include "util/concurrent/safe_map.h"
#include "util/container_of.h"
#include "util/hlist.h"
#include "util/list.h"

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

    dsm_wkr_ctx->page_counter = 0;

    if (dsm_safe_map_init(&dsm_wkr_ctx->managerID_to_nodeID, DSM_WKR_NODE_HASH_BITS) <
        0) {
        return -1;
    }

    if (dsm_safe_map_init(&dsm_wkr_ctx->requests_reg, DSM_WKR_PAGE_HASH_BITS) < 0) {
        LOG_ERROR("[dsm_wkr_ctx_init] dsm_safe_map_init dsm_wkr_ctx->requests_reg");
        return -1;
    }

    if (dsm_safe_map_init(&dsm_wkr_ctx->locks_reg, DSM_WKR_PAGE_HASH_BITS) < 0) {
        LOG_ERROR("[dsm_wkr_ctx_init] dsm_safe_map_init dsm_wkr_ctx->locks_reg");
        return -1;
    }

    if (barrier_init(&dsm_wkr_ctx->sync.barr) < 0) {
        LOG_ERROR("barrier_init");
        return -1;
    }

    if (finalize_init(&dsm_wkr_ctx->sync.fina) < 0) {
        LOG_ERROR("finalize_init");
        return -1;
    }

    atomic_store(&dsm_wkr_ctx->uffd_count, 0);
    atomic_store(&dsm_wkr_ctx->data_count, 0);
    atomic_store(&dsm_wkr_ctx->inva_count, 0);

    return 0;
}

void dsm_wkr_ctx_cleanup() {
    // Cleanup du worker
    dsm_requests_reg_entry *entry_requests;
    dsm_locks_reg_entry *entry_locks;
    dsm_locks_list_entry *pos, *n;
    struct dsm_hlist_node *tmp;
    dsm_mgr_to_node_content *mgr_entry;
    unsigned int bkt;

    dsm_hash_safe_for_each_safe(&dsm_wkr_ctx->requests_reg, bkt, tmp, entry_requests,
                                node) {
        dsm_hlist_del_init(&entry_requests->node);
        pthread_mutex_destroy(&entry_requests->mutex);
        free(entry_requests);
    }
    dsm_safe_map_destroy(&dsm_wkr_ctx->requests_reg);

    dsm_hash_safe_for_each_safe(&dsm_wkr_ctx->locks_reg, bkt, tmp, entry_locks, node) {
        dsm_hlist_del_init(&entry_locks->node);
        dsm_safe_list_for_each_entry_safe(&entry_locks->locks_list, pos, n, node) {
            dsm_list_del(&pos->node);
            pthread_mutex_destroy(&pos->mutex);
            pthread_cond_destroy(&pos->cond);
            free(pos);
        }
        free(entry_locks);
    }
    dsm_safe_map_destroy(&dsm_wkr_ctx->locks_reg);

    dsm_hash_safe_for_each_safe(&dsm_wkr_ctx->managerID_to_nodeID, bkt, tmp, mgr_entry,
                                node) {
        dsm_hlist_del_init(&mgr_entry->node);
        free(mgr_entry);
    }
    dsm_safe_map_destroy(&dsm_wkr_ctx->managerID_to_nodeID);
    close(dsm_wkr_ctx->uffd);
    // Barriere et FINA
    free(dsm_wkr_ctx);
    dsm_wkr_ctx = NULL;
}

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

pthread_mutex_t *get_mgr_send_lock_for_page(unsigned long page_id) {
    dsm_mgr_to_node_content *manager = NULL;
    if (!get_mgr_convert_node(page_id, &manager)) {
        return NULL;
    }
    return &manager->to_node_content->send_lock;
}

dsm_requests_reg_entry *get_request_entry(unsigned long page_id) {
    dsm_requests_reg_entry *entry_map = NULL, *found = NULL;
    dsm_hash_safe_get(&dsm_wkr_ctx->requests_reg, entry_map, node, page_id) {
        if (entry_map->page_id == page_id) {
            found = entry_map;
            break;
        }
    }
    dsm_hash_safe_get_end(&dsm_wkr_ctx->requests_reg, page_id);
    return found;
}

static int treat_uffd_pagefault(struct uffd_msg *fault_msg) {

    __u64 addr = fault_msg->arg.pagefault.address;
    int page_id = addr_to_page_id(addr);

    LOG_INFO("Traitement d'un pagefault pour l'adresse %p (page_id %d)", (void *)addr,
             page_id);

    int flags = fault_msg->arg.pagefault.flags;
    int fd_sock_mgr = get_mgr_node_socket_for_page(page_id);
    pthread_mutex_t *send_lock = get_mgr_send_lock_for_page(page_id);
    if (send_lock == NULL)
        return -1;
    int ret = 0;

    page_mode_req mode = (flags & (UFFD_PAGEFAULT_FLAG_WRITE | UFFD_PAGEFAULT_FLAG_WP))
                             ? WRITE_REQ
                             : READ_REQ;
    dsm_requests_reg_entry *entry = get_request_entry(page_id);
    LOG_INFO("[uffd_fault] Thread %lu tente d'accéder à page %d en %s\n",
             (unsigned long)pthread_self(), page_id,
             (mode == WRITE_REQ ? "WRITE" : "READ"));

    if (mode == READ_REQ) {
        pthread_mutex_lock(&entry->mutex);
        if (entry->state < READ_SENT) {
            entry->state = READ_SENT;
            pthread_mutex_unlock(&entry->mutex);

            pthread_mutex_lock(send_lock);
            ret = send_wkr_req(page_id, READ_REQ, fd_sock_mgr);
            pthread_mutex_unlock(send_lock);
            goto exit_return;
        }
        pthread_mutex_unlock(&entry->mutex);

    } else if (mode == WRITE_REQ) {
        pthread_mutex_lock(&entry->mutex);
        if (entry->state < WRITE_SENT) {
            entry->state = WRITE_SENT;
            pthread_mutex_unlock(&entry->mutex);

            pthread_mutex_lock(send_lock);
            ret = send_wkr_req(page_id, WRITE_REQ, fd_sock_mgr);
            pthread_mutex_unlock(send_lock);
            goto exit_return;
        }
        pthread_mutex_unlock(&entry->mutex);
    }

exit_return:
    if (ret >= 0) {
        LOG_INFO("Succès du traitement du pagefault");
    }
    return ret;
}

void dispatch_call_uffd(void *arg) {
    dsm_msg_uffd_thread_ctx *msg_ctx = (dsm_msg_uffd_thread_ctx *)arg;
    struct uffd_msg *fault_msg = &msg_ctx->fault_msg;

    switch (fault_msg->event) {
        case UFFD_EVENT_PAGEFAULT:
            atomic_fetch_add(&dsm_wkr_ctx->uffd_count, 1);
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
 * @brief Traitement de la reception des données apres une requete.
 *
 * TODO - majda : gerer LOCK et UNLOCK
 *
 * @param msg_ctx
 * @return int
 */
static int treat_mgr_req_ack(dsm_msg_tcpss_thread_ctx *msg_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_MGR_REQ_ACK);

    dsm_msg_mgr_req_ack *payload;
    void *zero_page =
        calloc(1, EFFECTIVE_PAGESIZE); // alloue une plage de EFFECTIVE_PAGESIZE
                                       // contigue initialisée à 0
    dsm_locks_reg_entry *entry = NULL, *found = NULL;
    if (dsm_dsrlze_mgr_req_ack(msg_ctx->payload, msg_ctx->header.payload_len, &payload) <
        0) {
        LOG_ERROR("dsm_dsrlze_mgr_req_ack");
        return -1;
    }
    dsm_requests_reg_entry *req_entry = get_request_entry(payload->page_id);

    switch (payload->data_info) {
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
            LOG_ERROR("data_info inconnu dans le payload du CODE_MGR_REQ_ACK");
            break;
    }

    switch (payload->req_mode) {
        case READ_REQ:
            LOG_INFO("[treat_mgr_req_ack] READ_ACK reçu");
            pthread_mutex_lock(&req_entry->mutex);
            if (req_entry->state == READ_SENT) {
                req_entry->state = NONE;
            }
            pthread_mutex_unlock(&req_entry->mutex);
            break;

        case WRITE_REQ:
            // maybe or maybe not, but i think not
            // WEEE je dois supprimer les entrés de la requests_pending_list
            LOG_INFO("[treat_mgr_req_ack] WRITE_ACK reçu");
            pthread_mutex_lock(&req_entry->mutex);
            if (req_entry->state == WRITE_SENT) {
                req_entry->state = NONE;
            }
            pthread_mutex_unlock(&req_entry->mutex);
            break;

        case LOCK_REQ:
            LOG_INFO("[treat_mgr_req_ack] LOCK_ACK reçu");
            // Utilisation de locks_reg
            // Ici on a reçu une reponse a une demande de lock
            // On doit s'assurer qu'on débloque le bon thread avec locks_reg
            // locks_reg est une liste FIFO, donc on est sur que le 1er thread qui a
            // envoyé sa requete est le 1er a être réveillé
            // donc on parcours la liste a la recherche de la 1ere demande de lock
            // associé a cette page
            pthread_mutex_lock(&req_entry->mutex);
            if (req_entry->state == WRITE_SENT) {
                req_entry->state = NONE;
            }
            pthread_mutex_unlock(&req_entry->mutex);

            dsm_hash_safe_get(&dsm_wkr_ctx->locks_reg, entry, node, payload->page_id) {
                if (entry->page_id == payload->page_id) {
                    found = entry;
                    break;
                }
            }
            dsm_hash_safe_get_end(&dsm_wkr_ctx->locks_reg, payload->page_id);

            dsm_safe_list_lock(&found->locks_list);
            if (!dsm_list_empty(&found->locks_list.head)) {
                dsm_locks_list_entry *first = dsm_container_of(
                    found->locks_list.head.next, dsm_locks_list_entry, node);
                pthread_mutex_lock(&first->mutex);
                first->locked = 1;
                pthread_cond_signal(&first->cond);
                pthread_mutex_unlock(&first->mutex);
            }
            dsm_safe_list_unlock(&found->locks_list);
            break;

        case UNLOCK_REQ:
            LOG_ERROR("[treat_mgr_req_ack] Erreur Algo : UNLOCK_ACK reçu");
            break;

        default:
            LOG_ERROR("req_mode inconnu dans le payload du CODE_MGR_REQ_ACK");
            break;
    }

    LOG_INFO("Succès du traitement du CODE=%d", CODE_MGR_REQ_ACK);
    free(payload);
    free(zero_page);
    return 0;
error:
    free(payload);
    free(zero_page);
    return -1;
}

/**
 * @brief Traitement d'une demande d'invalidation
 *
 * @return int
 */
static int treat_mgr_inva_req(dsm_msg_tcpss_thread_ctx *msg_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_MGR_INVA_REQ);

    dsm_msg_mgr_inva_req payload;

    if (dsm_dsrlze_mgr_inva_req(msg_ctx->payload, msg_ctx->header.payload_len, &payload) <
        0) {
        LOG_ERROR("dsm_dsrlze_mgr_inva_req");
        return -1;
    }
    dsm_requests_reg_entry *req_entry = get_request_entry(payload.page_id);
    void *addr = (void *)(uintptr_t)page_id_to_addr(payload.page_id);

    if (req_entry) {
        pthread_mutex_lock(&req_entry->mutex);
        req_entry->state = NONE; /* reset : un nouveau fault pourra re-envoyer */
        pthread_mutex_unlock(&req_entry->mutex);
    }

    LOG_INFO("[treat_mgr_inva_req] Invalidation de la page %d", payload.page_id);
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
        if (ret < 0) {
            return -1;
        }
    }

    LOG_INFO("Succès du traitement du CODE=%d", CODE_MGR_INVA_REQ);
    return 0;
}

/**
 * @brief Traitement de la reception d'une demande de données de la part d'un manager
 *
 * @return int
 */
static int treat_mgr_data_req(dsm_msg_tcpss_thread_ctx *msg_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_MGR_DATA_REQ);

    dsm_msg_mgr_data_req payload;
    if (dsm_dsrlze_mgr_data_req(msg_ctx->payload, msg_ctx->header.payload_len, &payload) <
        0) {
        LOG_ERROR("dsm_dsrlze_mgr_data_req");
        return -1;
    }
    void *addr = (void *)page_id_to_addr(payload.page_id);
    void *data = malloc(EFFECTIVE_PAGESIZE);
    memcpy(data, addr, EFFECTIVE_PAGESIZE);

    if (payload.is_invalidate) {
        /**
         * On invalide la page
         */
        LOG_INFO("[treat_mgr_data_req] Invalidation de la page %d", payload.page_id);
        if (madvise(addr, EFFECTIVE_PAGESIZE, MADV_DONTNEED) < 0) {
            LOG_ERROR("madvise");
            return -1;
        }

    } else {
        if (pager_protect(dsm_wkr_ctx->uffd, payload.page_id,
                          UFFDIO_WRITEPROTECT_MODE_WP) < 0) {
            return -1;
        }
    }

    /**
     * envoyer dsm_msg_wkr_data_ack
     */
    pthread_mutex_lock(msg_ctx->send_lock);
    int ret = send_wkr_data_ack(payload.page_id, data, msg_ctx->sock);
    pthread_mutex_unlock(msg_ctx->send_lock);

    free(data);
    if (ret >= 0) {
        LOG_INFO("Succès du traitement du CODE=%d", CODE_MGR_DATA_REQ);
    }
    return ret;
}

static int treat_sync_barrier_ack() {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_SYNC_BARRIER_REQ);

    pthread_mutex_lock(&dsm_wkr_ctx->sync.barr.lock);
    int ret = barrier_trigger(&dsm_wkr_ctx->sync.barr);
    pthread_mutex_unlock(&dsm_wkr_ctx->sync.barr.lock);
    if (ret < 0) {
        return -1;
    }

    LOG_INFO("Succès du traitement du CODE=%d", CODE_SYNC_BARRIER_REQ);
    return 0;
}

static int treat_sync_finalize_ack() {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_SYNC_FINALIZE_REQ);

    pthread_mutex_lock(&dsm_wkr_ctx->sync.fina.lock);
    int ret = finalize_trigger(&dsm_wkr_ctx->sync.fina);
    pthread_mutex_unlock(&dsm_wkr_ctx->sync.fina.lock);
    if (ret < 0) {
        return -1;
    }

    LOG_INFO("Succès du traitement du CODE=%d", CODE_SYNC_FINALIZE_REQ);
    return 0;
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
        case CODE_MGR_REQ_ACK:
            if (treat_mgr_req_ack(msg_ctx) < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_MGR_REQ_ACK);
            }
            break;

        case CODE_MGR_INVA_REQ:
            atomic_fetch_add(&dsm_wkr_ctx->inva_count, 1);
            if (treat_mgr_inva_req(msg_ctx) < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_MGR_INVA_REQ);
            }
            break;

        case CODE_MGR_DATA_REQ:
            atomic_fetch_add(&dsm_wkr_ctx->data_count, 1);
            if (treat_mgr_data_req(msg_ctx) < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_MGR_DATA_REQ);
            }
            break;

        case CODE_SYNC_BARRIER_ACK:
            if (treat_sync_barrier_ack() < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_SYNC_BARRIER_REQ);
            }
            break;

        case CODE_SYNC_FINALIZE_ACK:
            if (treat_sync_finalize_ack() < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_SYNC_FINALIZE_REQ);
            }
            break;

        default:
            LOG_WARNING("Code de message TCP inconnu ou non traitable: %d", code);
    }

    free(msg_ctx->payload);
    free(msg_ctx);
    return;
}
