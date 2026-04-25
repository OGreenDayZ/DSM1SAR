#include "worker.h"

#include "debug/debug.h"
#include "internal.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "network/pager/pager_msg.h"
#include "pager/pager.h"

#include <linux/userfaultfd.h>

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

    if (dsm_safe_map_init(&dsm_wkr_ctx->barrier_reg, DSM_WKR_PAGE_HASH_BITS) < 0) {
        return -1;
    }

    dsm_wkr_ctx->sync_finalize.target_count = 0;
    if (pthread_mutex_init(&dsm_wkr_ctx->sync_finalize.targ_c_mutex, NULL) != 0) {
        LOG_ERROR("pthread_mutex_init : targ_c_mutex");
        return -1;
    }

    dsm_wkr_ctx->sync_finalize.finalize_count = 0;
    if (pthread_cond_init(&dsm_wkr_ctx->sync_finalize.finalize_cond, NULL) != 0) {
        LOG_ERROR("pthread_cond_init : finalize_cond");
        return -1;
    }

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

/**
 * TODO MAJDA : c'est ton ancien handle_pagefault_worker
 * readapter en utilisant les fonctions au dessus pr les pageID et managerID,
 * normalement les non static suffisent ?
 */
static int treat_uffd_pagefault(struct uffd_msg *fault_msg) {
    __u64 addr = fault_msg->arg.pagefault.address;
    int page_id = addr_to_page_id(addr);
    int flags = fault_msg->arg.pagefault.flags;
    int fd_sock_mgr = get_mgr_node_socket_for_page(page_id);

    if (flags & UFFD_PAGEFAULT_FLAG_WRITE || flags & UFFD_PAGEFAULT_FLAG_WP) {
        /**
         * Accés en écriture à une page non présente en mémoire :
         * prévenir le manager de cette écriture, pour recuperer les données de la page
         * OU
         * Accès en écriture sur une page présente et protégée en écriture :
         * prévenir le manager pour qu'il invalide la page chez les autres workers,
         * puis retirer la protection d'écriture localement.
         */
        return send_wkr_req(page_id, WRITE_REQ, fd_sock_mgr);
    }
    /*
     * Accés a une page non présente en lecture :
     * envoyer une requete au manager qui gere cette page
     */
    return send_wkr_req(page_id, READ_REQ, fd_sock_mgr);
}

void dispatch_call_uffd(void *arg) {
    dsm_msg_uffd_thread_ctx *msg_ctx = (dsm_msg_uffd_thread_ctx *)arg;
    struct uffd_msg *fault_msg = &msg_ctx->fault_msg;

    switch (fault_msg->event) {
        case UFFD_EVENT_PAGEFAULT:

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

static int treat_mgr_inva_req();
static int treat_mgr_data_req();
static int treat_mgr_data_ack(dsm_msg_tcp_thread_ctx *msg_ctx) {
    dsm_msg_mgr_data_ack payload;
    if (dsm_dsrlze_mgr_data_ack(msg_ctx->payload, sizeof(msg_ctx->payload), &payload) <
        0) {
        LOG_ERROR("dsm_dsrlze_mgr_data_ack");
        return -1;
    }

    if (payload.is_empty) {
        // Ya des données
        if (pager_copy(dsm_wkr_ctx->uffd, payload.page_id, (__u64)payload.data,
                       (payload.req_mode == WRITE_REQ) ? 0 : UFFDIO_COPY_MODE_WP) < 0) {
            return -1;
        }
    } else {
        // pas de données
        /**
         * @note possible d'avoir pas de données sur une requete READ ?
         */
        void *zero_page =
            calloc(1, EFFECTIVE_PAGESIZE); // alloue une plage de EFFECTIVE_PAGESIZE
                                           // contigue initialisée à 0
        if (pager_copy(dsm_wkr_ctx->uffd, payload.page_id, (__u64)zero_page,
                       (payload.req_mode == WRITE_REQ) ? 0 : UFFDIO_COPY_MODE_WP) < 0) {
            return -1;
        }
    }
    return 0;
}
static int treat_mgr_lock_ack();

void dispatch_call_wkr(void *arg) {
    /**
     * Contient :
     * fd_sock de qui envoie le message
     * header
     * payload
     */
    dsm_msg_tcp_thread_ctx *msg_ctx = (dsm_msg_tcp_thread_ctx *)arg;

    int code = msg_ctx->header.code;

    switch (code) {
        case CODE_WKR_REQ:
            // treat

        case CODE_MGR_DATA_ACK:
            // treat
            LOG_INFO("Reception par le noeud %d du code CODE_MGR_DATA_ACK et traitement.",
                     dsm_ctx->node_id);
            treat_mgr_data_ack(msg_ctx);
            break;

        default:
            LOG_WARNING("Code de message TCP inconnu ou non traitable: %d", code);
    }

    // Ancien taff
    // if (code == CODE_PAGER_MANAGER_ACK) {
    //     dsm_msg_pgr_mgr_ack payload;
    //     if (dsm_dsrlze_pgr_mgr_ack(pbuf, buf_size, &payload) < 0)
    //         return -1;

    //     if (payload.flags & MGR_ACK_DATA) {
    //         /* Données reçues avec l'ACK */
    //         if (payload.mode == READ_REQ) {
    //             /* TODO : reception d'une reponse a une requete en lecture */
    //         } else if (payload.mode == WRITE_REQ) {
    //             /* mode = 0 : on ne reprotège pas en écriture ici */
    //             pager_copy(payload.page_id, (__u64)payload.data, 0);
    //         }
    //     } else {
    //         /* Pas de données */
    //         if (payload.mode == READ_REQ) {
    //             // initialiser page avec des 0
    //             void *data = malloc(EFFECTIVE_PAGESIZE);
    //             memset(data, 0, EFFECTIVE_PAGESIZE);
    //             pager_copy(payload.page_id, (__u64)data, 0);
    //             free(data);
    //         } else if (payload.mode == WRITE_REQ) {
    //             pager_writeprotect(payload.page_id);
    //         }
    //     }

    //     if (payload.flags & MGR_INVALIDATE) {
    //         /* TODO : invalider la page */
    //     }

    //     if (payload.flags & PGR_SYNC) {
    //         /* TODO : répondre au manager */
    //     }

    // } else if (code == CODE_MGR_DATA_REQ) {
    //     dsm_msg_mgr_data_req payload;
    //     if (dsm_dsrlze_mgr_data_req(pbuf, buf_size, &payload) < 0)
    //         return -1;

    //     uint8_t *data = (uint8_t *)(RANGE_START + payload.page_id *
    //     EFFECTIVE_PAGESIZE);

    //     if (payload.is_invalidate) {
    //         madvise(data, EFFECTIVE_PAGESIZE, MADV_DONTNEED);
    //         LOG_INFO("Page %u invalidée", payload.page_id);
    //     }

    //     return send_pgr_data(payload.page_id, data);
    // }

    free(msg_ctx->payload);
    free(msg_ctx);
}
