#include "manager.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug/debug.h"
#include "internal.h"
#include "network/daemon.h"
#include "network/manager/mgr_factory.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "network/pager/pager_msg.h"
#include "pager/pager.h"
#include "util/concurrent/drain_queue.h"
#include "util/concurrent/safe_list.h"
#include "util/concurrent/safe_map.h"
#include "util/container_of.h"
#include "util/hlist.h"
#include "util/list.h"

dsm_manager_ctx *dsm_mgr_ctx = NULL;

int dsm_mgr_ctx_init() {
    LOG_INFO("Initialisation du contexte du manager ...");

    if (dsm_mgr_ctx) {
        LOG_ERROR("dsm_mgr_ctx déjà initialisé ?!");
        return -1;
    }

    dsm_mgr_ctx = malloc(sizeof(dsm_manager_ctx));
    if (!dsm_mgr_ctx) {
        LOG_ERROR("malloc : dsm_mgr_ctx");
        return -1;
    }

    if (dsm_safe_map_init(&dsm_mgr_ctx->pages_reg, DSM_MGR_PAGE_HASH_BITS) < 0) {
        return -1;
    }

    unsigned int nb_workers =
        dsm_ctx->config.net.system.nb_nodes - dsm_ctx->config.net.system.nb_managers;
    pthread_mutex_init(&dsm_mgr_ctx->sync.lock, NULL);
    dsm_mgr_ctx->sync.missing_barr_reqs = nb_workers;
    dsm_mgr_ctx->sync.missing_fina_reqs = nb_workers;

    return 0;
}

static dsm_request_content *req_content_of(dsm_dq_node *n) {
    return dsm_container_of(n, dsm_request_content, node);
}

void dsm_mgr_ctx_cleanup() {

    if (!dsm_mgr_ctx) {
        return;
    }

    unsigned int bkt;
    struct dsm_hlist_node *tmp;
    dsm_page_content *page_pos;

    dsm_reader_info *reader_pos, *tmp_pos_r;

    dsm_hash_safe_for_each_safe(&dsm_mgr_ctx->pages_reg, bkt, tmp, page_pos, node) {

        dsm_safe_list_for_each_entry_safe(&page_pos->readers, reader_pos, tmp_pos_r,
                                          node) {
            dsm_list_del(&reader_pos->node);
            free(reader_pos);
        }
        dsm_safe_list_destroy(&page_pos->readers);

        dsm_dq_node *cur;
        int count = 0;
        while ((cur = dsm_dq_front(&page_pos->pending_reqs)) != NULL) {
            dsm_request_content *req_content = req_content_of(cur);
            dsm_dq_pop(&page_pos->pending_reqs);
            free(req_content);
            count++;
        }
        dsm_dq_destroy(&page_pos->pending_reqs);
        if (count > 0) {
            LOG_WARNING("Suppression de %d requêtes en queue pour la page %u", count,
                        page_pos->page_id);
        }

        pthread_mutex_destroy(&page_pos->head_req_ctx.lock);
        if (page_pos->head_req_ctx.data) {
            free(page_pos->head_req_ctx.data);
        }

        dsm_hlist_del_init(&page_pos->node);
        free(page_pos);
    }

    dsm_safe_map_destroy(&dsm_mgr_ctx->pages_reg);

    pthread_mutex_destroy(&dsm_mgr_ctx->sync.lock);

    free(dsm_mgr_ctx);
    dsm_mgr_ctx = NULL;
}

static dsm_request_content *make_req_content(dsm_msg_tcpss_thread_ctx *msg_ctx,
                                             dsm_msg_wkr_req *req) {
    dsm_request_content *req_content = malloc(sizeof(dsm_request_content));
    if (!req_content) {
        LOG_ERROR("malloc dsm_request_content");
        return NULL;
    }

    req_content->header = msg_ctx->header;
    req_content->info = *req;
    dsm_dq_node_init(&req_content->node);

    return req_content;
}

static dsm_page_content *make_page_content(unsigned int page_id) {
    dsm_page_content *page_content = malloc(sizeof(dsm_page_content));
    if (!page_content) {
        LOG_ERROR("malloc dsm_page_content");
        return NULL;
    }

    page_content->page_id = page_id;
    page_content->mode = PAGE_INIT;
    page_content->owner_id = -1;

    dsm_safe_list_init(&page_content->readers);

    dsm_dq_init(&page_content->pending_reqs);

    pthread_mutex_init(&page_content->head_req_ctx.lock, NULL);
    page_content->head_req_ctx.missing_acks = 0;
    page_content->head_req_ctx.promotion_only = false;
    page_content->head_req_ctx.data_size = 0;
    page_content->head_req_ctx.data = NULL;

    dsm_init_hlist_node(&page_content->node);

    return page_content;
}

/**
 * @warning : On a le droit de donner un accès sur out_page_content
 *            car la création de la page est définitive.
 *            De plus, chaque champ de page_content est implicitement protégé en écriture
 *            par la drain_queue (seul le drainer peut les modifier).
 *            Sauf pour @c readers, @c pending_reqs et @c head_req_ctx,
 *            qui sont protégés par leurs propres mécanismes de synchronisation.
 */
int mgr_push_req(dsm_msg_tcpss_thread_ctx *msg_ctx, dsm_msg_wkr_req *req,
                 dsm_page_content **out_page_content, bool *is_drainer) {

    LOG_INFO("Push d'une requête du worker (NODE=%d)...", msg_ctx->header.id);

    dsm_page_content *obj;
    dsm_page_content *page_content = make_page_content(req->page_id);
    if (!page_content) {
        return -1;
    }

    if (!dsm_hash_safe_add_unique(&dsm_mgr_ctx->pages_reg, &page_content->node, obj, node,
                                  page_id, req->page_id)) {
        free(page_content);
        page_content = obj; // Attention au write sur champs direct !! MUTEX NON ACQUIS
    }

    dsm_request_content *req_content = make_req_content(msg_ctx, req);
    if (!req_content) {
        return -1;
    }

    *is_drainer = dsm_dq_push(&page_content->pending_reqs, &req_content->node);
    if (out_page_content) {
        *out_page_content = page_content;
    }

    LOG_INFO("Requête du worker (NODE=%d) poussée avec succès (page_id=%u, mode=%d)",
             msg_ctx->header.id, req->page_id, req->mode);

    return 0;
}

static int treat_wkr_read_req(dsm_page_content *page_content,
                              dsm_request_content *req_content, bool *req_completed) {

    switch (page_content->mode) {

        case PAGE_INIT:
            if (treat_read_req_poss_init(page_content, req_content, req_completed) < 0) {
                return -1;
            }
            break;

        case PAGE_R:
            if (treat_read_req_poss_r(page_content, req_content, req_completed) < 0) {
                return -1;
            }
            break;

        case PAGE_RW:
            if (treat_read_req_poss_rw(page_content, req_content, req_completed) < 0) {
                return -1;
            }
            break;

        case PAGE_LOCKED:
            LOG_ERROR("Erreur Algo : LOCKED sans UNLOCK_REQ ?!");
            return -1;

        default:
            LOG_ERROR("Mode de possession de page inconnu : %d", page_content->mode);
            return -1;
    }
    return 0;
}

static int treat_wkr_write_req(dsm_page_content *page_content,
                               dsm_request_content *req_content, bool *req_completed) {

    switch (page_content->mode) {

        case PAGE_INIT:
            if (treat_write_req_poss_init(page_content, req_content, req_completed) < 0) {
                return -1;
            }
            break;

        case PAGE_R:
            if (treat_write_req_poss_r(page_content, req_content, req_completed) < 0) {
                return -1;
            }
            break;

        case PAGE_RW:
            if (treat_write_req_poss_rw(page_content, req_content, req_completed) < 0) {
                return -1;
            }
            break;

        case PAGE_LOCKED:
            LOG_ERROR("Erreur Algo : LOCKED sans UNLOCK_REQ ?!");
            return -1;

        default:
            LOG_ERROR("Mode de possession de page inconnu : %d", page_content->mode);
            return -1;
    }
    return 0;
}

static int treat_wkr_lock_req(dsm_page_content *page_content,
                              dsm_request_content *req_content, bool *req_completed) {

    switch (page_content->mode) {

        case PAGE_INIT:
            if (treat_lock_req_poss_init(page_content, req_content, req_completed) < 0) {
                return -1;
            }
            break;

        case PAGE_R:
            if (treat_lock_req_poss_r(page_content, req_content, req_completed) < 0) {
                return -1;
            }
            break;

        case PAGE_RW:
            if (treat_lock_req_poss_rw(page_content, req_content, req_completed) < 0) {
                return -1;
            }
            break;

        case PAGE_LOCKED:
            LOG_ERROR("Erreur Algo : LOCKED sans UNLOCK_REQ ?!");
            return -1;

        default:
            LOG_ERROR("Mode de possession de page inconnu : %d", page_content->mode);
            return -1;
    }
    return 0;
}

static void reset_head_req_ctx(dsm_page_content *page_content) {
    page_content->head_req_ctx.missing_acks = 0;
    page_content->head_req_ctx.promotion_only = false;
    page_content->head_req_ctx.data_size = 0;
    if (page_content->head_req_ctx.data) {
        free(page_content->head_req_ctx.data);
    }
    page_content->head_req_ctx.data = NULL;
}

/**
 * @brief : Va traiter un max de requêtes jusqu'à que :
 *  - la queue de requêtes devient vide
 *  - la requête en tête est bloquante (nécessite des ACKs)
 */
static int mgr_drain_reqs(dsm_page_content *page_content) {
    LOG_INFO("Drain : Début pour la page %u", page_content->page_id);

    dsm_dq_node *cur;
    while ((cur = dsm_dq_front(&page_content->pending_reqs)) != NULL) {
        dsm_request_content *req_content = req_content_of(cur);
        bool req_completed = false;
        switch (req_content->info.mode) {
            case READ_REQ:
                if (treat_wkr_read_req(page_content, req_content, &req_completed) < 0) {
                    return -1;
                }
                break;

            case WRITE_REQ:
                if (treat_wkr_write_req(page_content, req_content, &req_completed) < 0) {
                    return -1;
                }
                break;

            case LOCK_REQ:
                if (treat_wkr_lock_req(page_content, req_content, &req_completed) < 0) {
                    return -1;
                }
                break;

            default:
                LOG_ERROR("Mode de requête de page inconnu ou non traitable : %d",
                          req_content->info.mode);
                return -1;
        }

        if (!req_completed) {
            break;
        }

        /**
         * @warning : On ne pop que si elle est finalisée !
         */
        int empty = dsm_dq_pop(&page_content->pending_reqs);
        free(req_content);
        reset_head_req_ctx(page_content);
        if (empty) {
            break;
        }
    }
    LOG_INFO("Drain : Fin pour la page %u", page_content->page_id);
    return 0;
}

static dsm_page_content *find_page_content(uint32_t page_id) {
    dsm_page_content *page_content = NULL;
    if (!dsm_hash_safe_contains(&dsm_mgr_ctx->pages_reg, page_content, node, page_id,
                                page_id)) {
        return NULL;
    } else {
        return page_content;
    }
}

int mgr_try_unlock(dsm_msg_tcpss_thread_ctx *msg_ctx, dsm_msg_wkr_req *req,
                   dsm_page_content **out_page_content, bool *is_drainer) {

    LOG_INFO("Traitement d'une requête UNLOCK_REQ du worker (NODE=%d)...",
             msg_ctx->header.id);

    *out_page_content = find_page_content(req->page_id);
    if (!*out_page_content) {
        LOG_ERROR("Page %u non trouvée pour UNLOCK_REQ ?!", req->page_id);
        return -1;
    }

    if ((*out_page_content)->mode != PAGE_LOCKED) {
        LOG_ERROR("UNLOCK_REQ sur une page qui n'est pas LOCKED ?! (page_id=%u, mode=%d)",
                  req->page_id, (*out_page_content)->mode);
        return -1;
    }

    dsm_dq_node *front = dsm_dq_front(&(*out_page_content)->pending_reqs);
    if (!front) {
        LOG_ERROR("UNLOCK_REQ sur une page sans requête ?!", req->page_id);
        return -1;
    }

    dsm_request_content *front_req_content = req_content_of(front);
    if (front_req_content->info.mode != LOCK_REQ) {
        LOG_ERROR(
            "UNLOCK_REQ sur une page dont la requête en tête n'est pas un LOCK_REQ ?!"
            "(page_id=%u, front_req_mode=%d)",
            req->page_id, front_req_content->info.mode);
        return -1;
    }

    if (front_req_content->header.id != msg_ctx->header.id) {
        LOG_ERROR("UNLOCK_REQ par un worker qui n'est pas celui de la requête en tête ?!"
                  "(page_id=%u, front_req_worker_id=%u, unlock_req_worker_id=%u)",
                  req->page_id, front_req_content->header.id, msg_ctx->header.id);
        return -1;
    }

    (*out_page_content)->mode = PAGE_RW;
    *is_drainer = !dsm_dq_pop(&(*out_page_content)->pending_reqs);
    free(front_req_content);
    reset_head_req_ctx((*out_page_content));

    LOG_INFO("Requête UNLOCK_REQ traitée avec succès (page_id=%u)", req->page_id);

    return 0;
}

/**
 * @warning : Obsolète, voir handle_socket_event + drain_call_mgr
 */
// static int treat_wkr_req(dsm_msg_tcpss_thread_ctx *msg_ctx) {

//     dsm_msg_wkr_req req;
//     if (dsm_dsrlze_wkr_req(msg_ctx->payload, msg_ctx->header.payload_len, &req) < 0) {
//         return -1;
//     }

//     bool is_drainer = false;
//     dsm_page_content *page_content;
//     if (req.mode == READ_REQ || req.mode == WRITE_REQ || req.mode == LOCK_REQ) {

//         if (mgr_push_req(msg_ctx, &req, &page_content, &is_drainer) < 0) {
//             return -1;
//         }

//     } else if (req.mode == UNLOCK_REQ) {
//         if (try_unlock(msg_ctx, &req, &page_content, &is_drainer) < 0) {
//             return -1;
//         }

//     } else {
//         LOG_ERROR("Mode de requête de page inconnu : %d", req.mode);
//         return -1;
//     }

//     if (is_drainer && mgr_drain_reqs(page_content) < 0) {
//         return -1;
//     }

//     return 0;
// }

/**
 * @warning : Toujours lock le head_req_ctx de la page avant d'appeler cette fonction
 *
 */
static int try_send_req_ack(dsm_page_content *page_content, bool *is_drainer) {
    pending_reqs_head_ctx *head_ctx = &page_content->head_req_ctx;
    if (head_ctx->missing_acks > 0) {
        return 0;
    }

    dsm_request_content *req_content =
        req_content_of(dsm_dq_front(&page_content->pending_reqs));
    if (!req_content) {
        LOG_ERROR("Erreur Algo : Requête en tête de la page non trouvée ?!");
        return -1;
    }

    unsigned int cb_id = req_content->header.id;
    dsm_node_content *node_content;
    dsm_node_content *cb_node = NULL;

    dsm_hash_safe_get(&dsm_ctx->nodes_reg, node_content, node, cb_id) {
        if (node_content->id == cb_id) {
            cb_node = node_content;
        }
    }
    dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, cb_id);

    if (!cb_node) {
        LOG_ERROR("Erreur Algo : Noeud cible de la requete non trouvé ?!");
        return -1;
    }

    int ret = 0;
    pthread_mutex_lock(&cb_node->send_lock);
    if (head_ctx->data_size == 0) {
        if (head_ctx->promotion_only) {
            ret = send_mgr_req_ack(page_content->page_id, req_content->info.mode,
                                   PROMOTE_ONLY, NULL, cb_node->tcp_sock);
        } else {
            ret = send_mgr_req_ack(page_content->page_id, req_content->info.mode,
                                   EMPTY_INIT, NULL, cb_node->tcp_sock);
        }
    } else {
        ret = send_mgr_req_ack(page_content->page_id, req_content->info.mode, NOT_EMPTY,
                               head_ctx->data, cb_node->tcp_sock);
    }
    pthread_mutex_unlock(&cb_node->send_lock);

    if (ret < 0) {
        return -1;
    }

    if (page_content->mode == PAGE_LOCKED) {
        *is_drainer = false;

    } else {
        *is_drainer = !dsm_dq_pop(&page_content->pending_reqs);
        free(req_content);
        reset_head_req_ctx(page_content);
    }

    return 0;
}

static int inject_data(dsm_msg_tcpss_thread_ctx *msg_ctx, dsm_msg_wkr_data_ack *data_ack,
                       dsm_page_content **out_page_content, bool *is_drainer) {

    *out_page_content = find_page_content(data_ack->page_id);
    if (!*out_page_content) {
        LOG_ERROR("Page %u non trouvée pour DATA_ACK ?!", data_ack->page_id);
        return -1;
    }

    // est utile que dans les LOG_ERROR
    __attribute__((unused)) unsigned int msg_cb_id = msg_ctx->header.id;

    pthread_mutex_lock(&(*out_page_content)->head_req_ctx.lock);
    if ((*out_page_content)->head_req_ctx.missing_acks == 0) {
        LOG_ERROR("Erreur Algo : DATA_ACK reçu alors que la requête en tête n'attend pas "
                  "d'ACK ?! (page_id=%u, worker_id=%u)",
                  data_ack->page_id, msg_cb_id);
        goto unlock_return;
    }

    if ((*out_page_content)->head_req_ctx.promotion_only) {
        LOG_ERROR(
            "Erreur Algo : DATA_ACK reçu alors que la requête en tête n'attend que des "
            "ACK d'invalidation ?! (page_id=%u, worker_id=%u)",
            data_ack->page_id, msg_cb_id);
        goto unlock_return;
    }

    if ((*out_page_content)->head_req_ctx.data_size != 0) {
        LOG_ERROR(
            "Erreur Algo : DATA_ACK reçu alors que la requête en tête a déjà reçu des "
            "données ?! (page_id=%u, worker_id=%u)",
            data_ack->page_id, msg_cb_id);
        goto unlock_return;
    }

    (*out_page_content)->head_req_ctx.missing_acks--;
    (*out_page_content)->head_req_ctx.data_size = EFFECTIVE_PAGESIZE;

    (*out_page_content)->head_req_ctx.data = malloc(EFFECTIVE_PAGESIZE);
    if (!(*out_page_content)->head_req_ctx.data) {
        LOG_ERROR("malloc data page pour inject_data");
        goto unlock_return;
    }
    memcpy((*out_page_content)->head_req_ctx.data, data_ack->data, EFFECTIVE_PAGESIZE);

    try_send_req_ack(*out_page_content, is_drainer);

    pthread_mutex_unlock(&(*out_page_content)->head_req_ctx.lock);

    return 0;

unlock_return:
    pthread_mutex_unlock(&(*out_page_content)->head_req_ctx.lock);
    return -1;
}

static int inject_inva(dsm_msg_tcpss_thread_ctx *msg_ctx, dsm_msg_wkr_inva_ack *inva_ack,
                       dsm_page_content **out_page_content, bool *is_drainer) {

    *out_page_content = find_page_content(inva_ack->page_id);
    if (!*out_page_content) {
        LOG_ERROR("Page %u non trouvée pour INVA_ACK ?!", inva_ack->page_id);
        return -1;
    }

    /**
     * @warning : owner_id n'est pas == -1, vérification faite avant l'appel à inject_data
     */
    if ((unsigned int)(*out_page_content)->owner_id == msg_ctx->header.id) {
        LOG_ERROR("Erreur Algo : INVA_ACK d'un worker qui est proprio de la page ?! "
                  "(page_id=%u, worker_id=%u)",
                  inva_ack->page_id, msg_ctx->header.id);
        return -1;
    }

    pthread_mutex_lock(&(*out_page_content)->head_req_ctx.lock);
    if ((*out_page_content)->head_req_ctx.missing_acks == 0) {
        LOG_ERROR("Erreur Algo : INVA_ACK reçu alors que la requête en tête n'attend pas "
                  "d'ACK ?! (page_id=%u, worker_id=%u)",
                  inva_ack->page_id, msg_ctx->header.id);
        goto unlock_return;
    }

    (*out_page_content)->head_req_ctx.missing_acks--;

    try_send_req_ack(*out_page_content, is_drainer);

    pthread_mutex_unlock(&(*out_page_content)->head_req_ctx.lock);

    return 0;

unlock_return:
    pthread_mutex_unlock(&(*out_page_content)->head_req_ctx.lock);
    return -1;
}

static int treat_wkr_data_ack(dsm_msg_tcpss_thread_ctx *msg_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ... du NODE=%d | DATA_ACK", CODE_WKR_DATA_ACK,
             msg_ctx->header.id);

    dsm_msg_wkr_data_ack *data_ack;
    if (dsm_dsrlze_wkr_data_ack(msg_ctx->payload, msg_ctx->header.payload_len,
                                &data_ack) < 0) {
        return -1;
    }

    bool is_drainer = false;
    dsm_page_content *page_content;
    if (inject_data(msg_ctx, data_ack, &page_content, &is_drainer) < 0) {
        return -1;
    }

    if (is_drainer && mgr_drain_reqs(page_content) < 0) {
        return -1;
    }

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_DATA_ACK);
    free(data_ack);
    return 0;
}

static int treat_wkr_inva_ack(dsm_msg_tcpss_thread_ctx *msg_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ... du NODE=%d | INVA_ACK", CODE_WKR_INVA_ACK,
             msg_ctx->header.id);

    dsm_msg_wkr_inva_ack inva_ack;
    if (dsm_dsrlze_wkr_inva_ack(msg_ctx->payload, msg_ctx->header.payload_len,
                                &inva_ack) < 0) {
        return -1;
    }

    bool is_drainer = false;
    dsm_page_content *page_content;
    if (inject_inva(msg_ctx, &inva_ack, &page_content, &is_drainer) < 0) {
        return -1;
    }

    if (is_drainer && mgr_drain_reqs(page_content) < 0) {
        return -1;
    }

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_INVA_ACK);
    return 0;
}

static int treat_sync_barrier_req(__attribute__((unused)) unsigned int sender_id) {
    LOG_INFO("Traitement d'un CODE=%d ... du NODE=%d | BARRIER_REQ",
             CODE_SYNC_BARRIER_REQ, sender_id);

    pthread_mutex_lock(&dsm_mgr_ctx->sync.lock);

    if (dsm_mgr_ctx->sync.missing_barr_reqs == 0) {
        LOG_ERROR("Erreur Algo : BARRIER_REQ reçu alors missing_barr_reqs == 0 ?!");
        pthread_mutex_unlock(&dsm_mgr_ctx->sync.lock);
        return -1;
    }

    dsm_mgr_ctx->sync.missing_barr_reqs--;

    if (dsm_mgr_ctx->sync.missing_barr_reqs == 0) {

        unsigned int bkt;
        dsm_node_content *node_content;
        dsm_hash_safe_for_each(&dsm_ctx->nodes_reg, bkt, node_content, node) {
            pthread_mutex_lock(&node_content->send_lock);
            int ret = send_sync_barr_ack(node_content->tcp_sock);
            pthread_mutex_unlock(&node_content->send_lock);
            if (ret < 0) {
                pthread_mutex_unlock(&dsm_mgr_ctx->sync.lock);
                dsm_hash_safe_unlock(&dsm_ctx->nodes_reg, bkt);
                return -1;
            }
        }

        dsm_mgr_ctx->sync.missing_barr_reqs =
            dsm_ctx->config.net.system.nb_nodes - dsm_ctx->config.net.system.nb_managers;
    }

    pthread_mutex_unlock(&dsm_mgr_ctx->sync.lock);

    LOG_INFO("Succès du traitement du CODE=%d", CODE_SYNC_BARRIER_REQ);
    return 0;
}

static int treat_sync_finalize_req(__attribute__((unused)) unsigned int sender_id) {
    LOG_INFO("Traitement d'un CODE=%d ... du NODE=%d", CODE_SYNC_FINALIZE_REQ, sender_id);

    pthread_mutex_lock(&dsm_mgr_ctx->sync.lock);

    if (dsm_mgr_ctx->sync.missing_fina_reqs == 0) {
        LOG_ERROR("Erreur Algo : FINALIZE_REQ reçu alors missing_fina_reqs == 0 ?!");
        pthread_mutex_unlock(&dsm_mgr_ctx->sync.lock);
        return -1;
    }

    dsm_mgr_ctx->sync.missing_fina_reqs--;

    if (dsm_mgr_ctx->sync.missing_fina_reqs == 0) {

        unsigned int bkt;
        dsm_node_content *node_content;
        dsm_hash_safe_for_each(&dsm_ctx->nodes_reg, bkt, node_content, node) {
            pthread_mutex_lock(&node_content->send_lock);
            int ret = send_sync_fina_ack(node_content->tcp_sock);
            pthread_mutex_unlock(&node_content->send_lock);
            if (ret < 0) {
                pthread_mutex_unlock(&dsm_mgr_ctx->sync.lock);
                dsm_hash_safe_unlock(&dsm_ctx->nodes_reg, bkt);
                return -1;
            }
        }

        atomic_store(&dsm_dmn_ctx->state, DSM_DMN_STATE_TERMINATING);
    }

    pthread_mutex_unlock(&dsm_mgr_ctx->sync.lock);

    LOG_INFO("Succès du traitement du CODE=%d", CODE_SYNC_FINALIZE_REQ);
    return 0;
}

void drain_call_mgr(void *arg) {
    dsm_page_content *page_content = (dsm_page_content *)arg;
    if (mgr_drain_reqs(page_content) < 0) {
        LOG_WARNING("Message non traité : CODE=%d", CODE_WKR_REQ);
    }
    return;
}

void dispatch_call_mgr(void *arg) {
    dsm_msg_tcpss_thread_ctx *msg_ctx = (dsm_msg_tcpss_thread_ctx *)arg;

    switch (msg_ctx->header.code) {
        case CODE_WKR_REQ:
            /**
             * @warning : Obsolète, voir handle_socket_event + drain_call_mgr
             */
            // if (treat_wkr_req(msg_ctx) < 0) {
            //     LOG_WARNING("Message non traité : CODE=%d", CODE_WKR_REQ);
            // }
            LOG_ERROR("Erreur Algo : CODE_WKR_REQ traité dans dispatch_call_mgr ?! "
                      "(obsolète, voir handle_socket_event + drain_call_mgr)");
            break;

        case CODE_WKR_DATA_ACK:
            if (treat_wkr_data_ack(msg_ctx) < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_WKR_DATA_ACK);
            }
            break;

        case CODE_WKR_INVA_ACK:
            if (treat_wkr_inva_ack(msg_ctx) < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_WKR_INVA_ACK);
            }
            break;

        case CODE_SYNC_BARRIER_REQ:
            if (treat_sync_barrier_req(msg_ctx->header.id) < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_SYNC_BARRIER_REQ);
            }
            break;

        case CODE_SYNC_FINALIZE_REQ:
            if (treat_sync_finalize_req(msg_ctx->header.id) < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_SYNC_FINALIZE_REQ);
            }
            break;

        default:
            LOG_WARNING("Code de message TCP inconnu ou non traitable: %d",
                        msg_ctx->header.code);
    }

    free(msg_ctx->payload);
    free(msg_ctx);
    return;
}