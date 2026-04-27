#include "worker.h"

#include "debug/debug.h"
#include "internal.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "network/pager/pager_msg.h"
#include "pager/pager.h"

#include <linux/userfaultfd.h>
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
    dsm_wkr_ctx->local_cursor = 0;

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

/**
 * TODO MAJDA : c'est ton ancien handle_pagefault_worker
 * readapter en utilisant les fonctions au dessus pr les pageID et managerID,
 * normalement les non static suffisent ?
 */
static int treat_uffd_pagefault(struct uffd_msg *fault_msg) {
    printf(
        "[treat_uffd_pagefault] je suis dans la fonction de traitement du pagefault\n");
    __u64 addr = fault_msg->arg.pagefault.address;
    int page_id = addr_to_page_id(addr);
    int flags = fault_msg->arg.pagefault.flags;
    int fd_sock_mgr = get_mgr_node_socket_for_page(page_id);

    printf("[treat_uffd_pagefault] je check les flags.\n");
    printf("[treat_uffd_pagefault] UFFD_PAGEFAULT_FLAG_WRITE : %d\n",
           flags & UFFD_PAGEFAULT_FLAG_WRITE);
    printf("[treat_uffd_pagefault] UFFD_PAGEFAULT_FLAG_WP : %d\n",
           flags & UFFD_PAGEFAULT_FLAG_WP);
    if (flags & UFFD_PAGEFAULT_FLAG_WRITE || flags & UFFD_PAGEFAULT_FLAG_WP) {
        /**
         * Accés en écriture à une page non présente en mémoire :
         * prévenir le manager de cette écriture, pour recuperer les données de la page
         * OU
         * Accès en écriture sur une page présente et protégée en écriture :
         * prévenir le manager pour qu'il invalide la page chez les autres workers,
         * puis retirer la protection d'écriture localement.
         */
        LOG_INFO("Pagefault en écriture sur la page %d, envoi d'une requete WRITE_REQ au "
                 "manager",
                 page_id);
        printf("[treat_uffd_pagefault] Pagefault en écriture sur la page %d, envoi d'une "
               "requete WRITE_REQ au "
               "manager\n",
               page_id);
        return send_wkr_req(page_id, WRITE_REQ, fd_sock_mgr);
    }
    /*
     * Accés a une page non présente en lecture :
     * envoyer une requete au manager qui gere cette page
     */
    printf("[treat_uffd_pagefault] Pagefault en lecture sur la page %d, envoi d'une "
           "requete READ_REQ au "
           "manager\n",
           page_id);
    printf("[treat_uffd_pagefault] flags : %d, should be empty\n", flags);

    return send_wkr_req(page_id, READ_REQ, fd_sock_mgr);
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
static int treat_mgr_inva_req(dsm_msg_tcp_thread_ctx *msg_ctx) {
    dsm_msg_mgr_inva_req payload;
    if (dsm_dsrlze_mgr_inva_req(msg_ctx->payload, sizeof(msg_ctx->payload), &payload) <
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
        return send_wkr_inva_ack(payload.page_id, msg_ctx->sock);
    }
    return 0;
}

/**
 * @brief Traitement de la reception d'une demande de données de la part d'un manager
 *
 * @return int
 */
static int treat_mgr_data_req(dsm_msg_tcp_thread_ctx *msg_ctx) {
    dsm_msg_mgr_data_req payload;
    if (dsm_dsrlze_mgr_data_req(msg_ctx->payload, sizeof(msg_ctx->payload), &payload) <
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
    int ret = send_wkr_data_ack(payload.page_id, (uint8_t *)addr, msg_ctx->sock);
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
static int treat_mgr_req_ack(dsm_msg_tcp_thread_ctx *msg_ctx) {
    dsm_msg_mgr_req_ack payload;
    void *zero_page =
        calloc(1, EFFECTIVE_PAGESIZE); // alloue une plage de EFFECTIVE_PAGESIZE
                                       // contigue initialisée à 0
    if (dsm_dsrlze_mgr_req_ack(msg_ctx->payload, sizeof(msg_ctx->payload), &payload) <
        0) {
        LOG_ERROR("dsm_dsrlze_mgr_req_ack");
        return -1;
    }

    switch (payload.is_empty) {
        case EMPTY_INIT:
            if (pager_copy(dsm_wkr_ctx->uffd, payload.page_id, (__u64)zero_page,
                           (payload.req_mode == WRITE_REQ || payload.req_mode == LOCK_REQ)
                               ? 0
                               : UFFDIO_COPY_MODE_WP) < 0) {
                goto error;
            }
            break;
        case NOT_EMPTY:
            if (pager_copy(dsm_wkr_ctx->uffd, payload.page_id, (__u64)payload.data,
                           (payload.req_mode == WRITE_REQ || payload.req_mode == LOCK_REQ)
                               ? 0
                               : UFFDIO_COPY_MODE_WP) < 0) {
                goto error;
            }
            break;
        case PROMOTE_ONLY:
            if (pager_writeprotect(dsm_wkr_ctx->uffd, payload.page_id) < 0) {
                goto error;
            }
            break;
        default:
            break;
    }

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
    dsm_msg_tcp_thread_ctx *msg_ctx = (dsm_msg_tcp_thread_ctx *)arg;

    int code = msg_ctx->header.code;

    switch (code) {
        case CODE_WKR_REQ:
            // treat

        case CODE_MGR_REQ_ACK:
            // treat
            LOG_INFO("Reception par le noeud %d du code CODE_MGR_REQ_ACK et traitement.",
                     dsm_ctx->node_id);
            treat_mgr_req_ack(msg_ctx);
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
