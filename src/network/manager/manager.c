#include "manager.h"
#include "debug/debug.h"
#include "network/message/serializer.h"
#include "pager/pager.h"

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

    return 0;
}

void dsm_mgr_ctx_cleanup();

static dsm_request_content *make_req_content(dsm_msg_tcpss_thread_ctx *msg_ctx,
                                             dsm_msg_wkr_req *req) {
    dsm_request_content *req_content = malloc(sizeof(dsm_request_content));
    if (!req_content) {
        LOG_ERROR("malloc dsm_request_content");
        return NULL;
    }

    req_content->sock = msg_ctx->sock;
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
    page_content->head_req_ctx.is_missing_data = false;
    page_content->head_req_ctx.data_size = 0;
    page_content->head_req_ctx.data = NULL;

    dsm_init_hlist_node(&page_content->node);

    return page_content;
}

static dsm_request_content *req_content_of(dsm_dq_node *n) {
    return dsm_container_of(n, dsm_request_content, node);
}

/**
 * @warning : On a le droit de donner un accès sur out_page_content
 *            car la création de la page est définitive.
 *            De plus, chaque champ de page_content est implicitement protégé en écriture
 *            par la drain_queue (seul le drainer peut les modifier).
 *            Sauf pour @c readers, @c pending_reqs et @c head_req_ctx,
 *            qui sont protégés par leurs propres mécanismes de synchronisation.
 */
static int push_req(dsm_msg_tcpss_thread_ctx *msg_ctx, dsm_msg_wkr_req *req,
                    bool *is_drainer, dsm_page_content **out_page_content) {

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
    return 0;
}

static int treat_wkr_read_req(dsm_page_content *page_content,
                              dsm_request_content *req_content, bool *is_req_completed) {
    // TODO
    return 0;
}

static int treat_wkr_write_req(dsm_page_content *page_content,
                               dsm_request_content *req_content, bool *is_req_completed) {
    // TODO
    return 0;
}

static int treat_wkr_lock_req(dsm_page_content *page_content,
                              dsm_request_content *req_content, bool *is_req_completed) {
    // TODO
    return 0;
}

/**
 * @brief : Va traiter un max de requêtes jusqu'à que :
 *  - la queue de requêtes devient vide
 *  - la requête en tête est bloquante (nécessite des ACKs)
 */
static int drain_reqs(dsm_page_content *page_content) {
    dsm_dq_node *cur;
    while ((cur = dsm_dq_front(&page_content->pending_reqs)) != NULL) {
        dsm_request_content *req_content = req_content_of(cur);
        bool req_completed;
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

        int empty = dsm_dq_pop(&page_content->pending_reqs);
        if (!req_completed || empty) {
            break;
        }
    }
    return 0;
}

static int treat_wkr_req(dsm_msg_tcpss_thread_ctx *msg_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_WKR_REQ);

    dsm_msg_wkr_req req;
    if (dsm_dsrlze_wkr_req(msg_ctx->payload, msg_ctx->header.payload_len, &req) < 0) {
        return -1;
    }

    if (req.mode == READ_REQ || req.mode == WRITE_REQ || req.mode == LOCK_REQ) {
        bool is_drainer;
        dsm_page_content *page_content;
        if (push_req(msg_ctx, &req, &is_drainer, &page_content) < 0) {
            return -1;
        }

        if (is_drainer) {
            if (drain_reqs(page_content) < 0) {
                return -1;
            }
        }

    } else if (req.mode == UNLOCK_REQ) {
    } else {
        LOG_WARNING("Mode de requête de page inconnu : %d", req.mode);
        return -1;
    }

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_REQ);
    return 0;
}

static int treat_wkr_data_ack(dsm_msg_tcpss_thread_ctx *msg_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_WKR_DATA_ACK);

    // TODO

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_DATA_ACK);
    return 0;
}

static int treat_wkr_inva_ack(dsm_msg_tcpss_thread_ctx *msg_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_WKR_INVA_ACK);

    // TODO

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_INVA_ACK);
    return 0;
}

void dispatch_call_mgr(void *arg) {
    dsm_msg_tcpss_thread_ctx *msg_ctx = (dsm_msg_tcpss_thread_ctx *)arg;

    switch (msg_ctx->header.code) {
        case CODE_WKR_REQ:
            if (treat_wkr_req(msg_ctx) < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_WKR_REQ);
            }
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

        default:
            LOG_WARNING("Code de message TCP inconnu ou non traitable: %d",
                        msg_ctx->header.code);
    }

    free(msg_ctx->payload);
    free(msg_ctx);
    return;
}