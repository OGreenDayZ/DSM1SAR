#include "mgr_factory.h"

#include "debug/debug.h"
#include "internal.h"
#include "network/manager/manager.h"
#include "network/message/serializer.h"
#include "network/pager/pager_msg.h"
#include "pager/pager.h"

/**
 * @note : cb = callback, le worker qui a initié la requete
 * @note : owner = proprio de la page avant traitement de la requete
 */

/* TODO FACTO + TRAITER CAS OU PROPRIO==DEMANDEUR INCOHERENCE R->R ?*/
int treat_read_req_poss_init(dsm_page_content *page_content,
                             dsm_request_content *req_content, bool *req_completed) {

    LOG_INFO("Traitement d'un CODE=%d ... du NODE=%d| READ_REQ sur PAGE_INIT",
             CODE_WKR_REQ, req_content->header.id);

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

    dsm_reader_info *reader_info = malloc(sizeof(dsm_reader_info));
    if (!reader_info) {
        LOG_ERROR("malloc dsm_reader_info");
        return -1;
    }
    reader_info->worker_id = cb_id;
    dsm_safe_list_add(&page_content->readers, &reader_info->node);

    pthread_mutex_lock(&cb_node->send_lock);
    int ret = send_mgr_req_ack(page_content->page_id, req_content->info.mode, EMPTY_INIT,
                               NULL, cb_node->tcp_sock);
    pthread_mutex_unlock(&cb_node->send_lock);

    if (ret < 0) {
        return -1;
    }

    *req_completed = true;

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_REQ);
    return 0;
}

int treat_read_req_poss_r(dsm_page_content *page_content,
                          dsm_request_content *req_content, bool *req_completed) {

    LOG_INFO("Traitement d'un CODE=%d ... du NODE=%d| READ_REQ sur PAGE_R", CODE_WKR_REQ,
             req_content->header.id);

    unsigned int owner_id = page_content->owner_id;
    dsm_node_content *node_content;
    dsm_node_content *owner_node = NULL;

    dsm_hash_safe_get(&dsm_ctx->nodes_reg, node_content, node, owner_id) {
        if (node_content->id == owner_id) {
            owner_node = node_content;
        }
    }
    dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, owner_id);

    if (!owner_node) {
        LOG_ERROR("Erreur Algo : Noeud proprio de la page non trouvé ?!");
        return -1;
    }

    dsm_reader_info *reader_info = malloc(sizeof(dsm_reader_info));
    if (!reader_info) {
        LOG_ERROR("malloc dsm_reader_info");
        return -1;
    }
    reader_info->worker_id = req_content->header.id;
    dsm_safe_list_add(&page_content->readers, &reader_info->node);

    page_content->head_req_ctx.missing_acks = 1;

    pthread_mutex_lock(&owner_node->send_lock);
    int ret = send_mgr_data_req(page_content->page_id, false, owner_node->tcp_sock);
    pthread_mutex_unlock(&owner_node->send_lock);

    if (ret < 0) {
        return -1;
    }

    *req_completed =
        false; // Meme false on explicitera toujours pour toutes les fonctions !

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_REQ);
    return 0;
}

// TODO FACTO EN return treat_read_req_poss_r ?
int treat_read_req_poss_rw(dsm_page_content *page_content,
                           dsm_request_content *req_content, bool *req_completed) {

    LOG_INFO("Traitement d'un CODE=%d ... du NODE=%d| READ_REQ sur PAGE_RW", CODE_WKR_REQ,
             req_content->header.id);
    unsigned int owner_id = page_content->owner_id;
    dsm_node_content *node_content;
    dsm_node_content *owner_node = NULL;

    dsm_hash_safe_get(&dsm_ctx->nodes_reg, node_content, node, owner_id) {
        if (node_content->id == owner_id) {
            owner_node = node_content;
        }
    }
    dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, owner_id);

    if (!owner_node) {
        LOG_ERROR("Erreur Algo : Noeud proprio de la page non trouvé ?!");
        return -1;
    }

    dsm_reader_info *reader_info = malloc(sizeof(dsm_reader_info));
    if (!reader_info) {
        LOG_ERROR("malloc dsm_reader_info");
        return -1;
    }
    reader_info->worker_id = req_content->header.id;
    dsm_safe_list_add(&page_content->readers, &reader_info->node);

    page_content->mode = PAGE_R;
    page_content->head_req_ctx.missing_acks = 1;

    pthread_mutex_lock(&owner_node->send_lock);
    int ret = send_mgr_data_req(page_content->page_id, false, owner_node->tcp_sock);
    pthread_mutex_unlock(&owner_node->send_lock);

    if (ret < 0) {
        return -1;
    }

    *req_completed = false;

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_REQ);
    return 0;
}

int treat_write_req_poss_init(dsm_page_content *page_content,
                              dsm_request_content *req_content, bool *req_completed) {

    LOG_INFO("Traitement d'un CODE=%d ... du NODE=%d| WRITE_REQ sur PAGE_INIT",
             CODE_WKR_REQ, req_content->header.id);
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

    dsm_reader_info *pos, *n;
    bool cb_is_reader = false;
    dsm_safe_list_for_each_entry_safe(&page_content->readers, pos, n, node) {

        if (pos->worker_id == cb_id) {
            cb_is_reader = true;

        } else {
            dsm_node_content *reader_node;
            bool found = false;
            int ret = 0;
            dsm_hash_safe_get(&dsm_ctx->nodes_reg, reader_node, node, pos->worker_id) {
                if (!found && reader_node->id == pos->worker_id) {
                    pthread_mutex_lock(&reader_node->send_lock);
                    ret = send_mgr_inva_req(page_content->page_id, false,
                                            reader_node->tcp_sock);
                    pthread_mutex_unlock(&reader_node->send_lock);
                    found = true;
                }
            }
            dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, pos->worker_id);

            if (ret < 0) {
                return -1;
            }

            if (!found) {
                LOG_ERROR("Erreur Algo : Noeud lecteur de la page non trouvé ?!");
                return -1;
            }
        }

        dsm_list_del(&pos->node);
        free(pos);
    }

    page_content->mode = PAGE_RW;
    page_content->owner_id = cb_id;
    data_info info = cb_is_reader ? PROMOTE_ONLY : EMPTY_INIT;

    pthread_mutex_lock(&cb_node->send_lock);
    int ret = send_mgr_req_ack(page_content->page_id, req_content->info.mode, info, NULL,
                               cb_node->tcp_sock);
    pthread_mutex_unlock(&cb_node->send_lock);

    if (ret < 0) {
        return -1;
    }

    *req_completed = true;

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_REQ);
    return 0;
}

int treat_write_req_poss_r(dsm_page_content *page_content,
                           dsm_request_content *req_content, bool *req_completed) {

    LOG_INFO("Traitement d'un CODE=%d ... du NODE=%d| WRITE_REQ sur PAGE_R", CODE_WKR_REQ,
             req_content->header.id);
    dsm_node_content *node_content;

    unsigned int owner_id = page_content->owner_id;
    dsm_node_content *owner_node = NULL;

    dsm_hash_safe_get(&dsm_ctx->nodes_reg, node_content, node, owner_id) {
        if (node_content->id == owner_id) {
            owner_node = node_content;
        }
    }
    dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, owner_id);

    if (!owner_node) {
        LOG_ERROR("Erreur Algo : Noeud proprio de la page non trouvé ?!");
        return -1;
    }

    unsigned int cb_id = req_content->header.id;
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

    bool cb_is_reader = false;

    dsm_reader_info *pos, *n;
    dsm_safe_list_for_each_entry_safe(&page_content->readers, pos, n, node) {

        if (pos->worker_id == cb_id) {
            cb_is_reader = true;

        } else {
            dsm_node_content *reader_node;
            bool found = false;
            int ret = 0;
            dsm_hash_safe_get(&dsm_ctx->nodes_reg, reader_node, node, pos->worker_id) {
                if (!found && reader_node->id == pos->worker_id) {
                    pthread_mutex_lock(&reader_node->send_lock);
                    ret = send_mgr_inva_req(page_content->page_id, false,
                                            reader_node->tcp_sock);
                    pthread_mutex_unlock(&reader_node->send_lock);
                    found = true;
                }
            }
            dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, pos->worker_id);

            if (ret < 0) {
                return -1;
            }

            if (!found) {
                LOG_ERROR("Erreur Algo : Noeud lecteur de la page non trouvé ?!");
                return -1;
            }
        }

        dsm_list_del(&pos->node);
        free(pos);
    }

    page_content->mode = PAGE_RW;
    /**
     * @note : Cette affection nous laisse toujours avec owner_id/owner_node valide vers
     * l'ancien propriétaire, celui déclaré au début de la fonction
     */
    page_content->owner_id = cb_id;

    int ret = 0;
    if (cb_is_reader || cb_id == owner_id) {

        if (cb_is_reader) {
            pthread_mutex_lock(&owner_node->send_lock);
            ret = send_mgr_inva_req(page_content->page_id, false, owner_node->tcp_sock);
            pthread_mutex_unlock(&owner_node->send_lock);

            if (ret < 0) {
                return -1;
            }
        }

        pthread_mutex_lock(&cb_node->send_lock);
        ret = send_mgr_req_ack(page_content->page_id, req_content->info.mode,
                               PROMOTE_ONLY, NULL, cb_node->tcp_sock);
        pthread_mutex_unlock(&cb_node->send_lock);

        if (ret < 0) {
            return -1;
        }

        page_content->head_req_ctx.missing_acks = 0;
        *req_completed = true;

    } else {

        pthread_mutex_lock(&owner_node->send_lock);
        ret = send_mgr_data_req(page_content->page_id, true, owner_node->tcp_sock);
        pthread_mutex_unlock(&owner_node->send_lock);

        if (ret < 0) {
            return -1;
        }

        page_content->head_req_ctx.missing_acks = 1;
        *req_completed = false;
    }

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_REQ);
    return 0;
}

int treat_write_req_poss_rw(dsm_page_content *page_content,
                            dsm_request_content *req_content, bool *req_completed) {

    LOG_INFO("Traitement d'un CODE=%d ... du NODE=%d| WRITE_REQ sur PAGE_RW",
             CODE_WKR_REQ, req_content->header.id);
    dsm_node_content *node_content;

    unsigned int owner_id = page_content->owner_id;
    dsm_node_content *owner_node = NULL;

    dsm_hash_safe_get(&dsm_ctx->nodes_reg, node_content, node, owner_id) {
        if (node_content->id == owner_id) {
            owner_node = node_content;
        }
    }
    dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, owner_id);

    if (!owner_node) {
        LOG_ERROR("Erreur Algo : Noeud proprio de la page non trouvé ?!");
        return -1;
    }

    unsigned int cb_id = req_content->header.id;
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

    page_content->owner_id = cb_id;

    int ret = 0;
    if (cb_id == owner_id) { // CHECK INCOHERENCE RW->RW ?, cas impossible

        pthread_mutex_lock(&cb_node->send_lock);
        ret = send_mgr_req_ack(page_content->page_id, req_content->info.mode,
                               PROMOTE_ONLY, NULL, cb_node->tcp_sock);
        pthread_mutex_unlock(&cb_node->send_lock);

        if (ret < 0) {
            return -1;
        }

        page_content->head_req_ctx.missing_acks = 0;
        *req_completed = true;

    } else {

        pthread_mutex_lock(&owner_node->send_lock);
        ret = send_mgr_data_req(page_content->page_id, true, owner_node->tcp_sock);
        pthread_mutex_unlock(&owner_node->send_lock);

        if (ret < 0) {
            return -1;
        }

        page_content->head_req_ctx.missing_acks = 1;
        *req_completed = false;
    }

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_REQ);
    return 0;
}

int treat_lock_req_poss_init(dsm_page_content *page_content,
                             dsm_request_content *req_content, bool *req_completed) {

    LOG_INFO("Traitement d'un CODE=%d ... | LOCK_REQ sur PAGE_INIT", CODE_WKR_REQ);

    int inva_ack_count = 0;
    bool cb_is_reader = false;
    dsm_reader_info *pos, *n;
    /**
     * @note : On lock, car is_sync = true, on peut recevoir CODE_WKR_INVA_ACK
     * avant la fin du traitement de la requete, et donc avant d'avoir mis à jour le
     * head_req_ctx
     */
    pthread_mutex_lock(&page_content->head_req_ctx.lock);
    dsm_safe_list_for_each_entry_safe(&page_content->readers, pos, n, node) {

        if (pos->worker_id == req_content->header.id) {
            cb_is_reader = true;

        } else {

            dsm_node_content *reader_node;
            bool found = false;
            int ret = 0;

            dsm_hash_safe_get(&dsm_ctx->nodes_reg, reader_node, node, pos->worker_id) {
                if (!found && reader_node->id == pos->worker_id) {
                    pthread_mutex_lock(&reader_node->send_lock);
                    ret = send_mgr_inva_req(page_content->page_id, true,
                                            reader_node->tcp_sock);
                    pthread_mutex_unlock(&reader_node->send_lock);
                    inva_ack_count++;
                    found = true;
                }
            }
            dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, pos->worker_id);

            if (ret < 0) {
                pthread_mutex_unlock(&page_content->head_req_ctx.lock);
                return -1;
            }

            if (!found) {
                pthread_mutex_unlock(&page_content->head_req_ctx.lock);
                LOG_ERROR("Erreur Algo : Noeud lecteur de la page non trouvé ?!");
                return -1;
            }
        }

        dsm_list_del(&pos->node);
        free(pos);
    }

    page_content->mode = PAGE_LOCKED;
    page_content->owner_id = req_content->header.id;
    page_content->head_req_ctx.missing_acks = inva_ack_count;
    if (cb_is_reader) {
        page_content->head_req_ctx.promotion_only = true;
    }

    pthread_mutex_unlock(&page_content->head_req_ctx.lock);

    *req_completed = false;

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_REQ);
    return 0;
}

int treat_lock_req_poss_r(dsm_page_content *page_content,
                          dsm_request_content *req_content, bool *req_completed) {

    LOG_INFO("Traitement d'un CODE=%d ... | LOCK_REQ sur PAGE_R", CODE_WKR_REQ);

    dsm_node_content *node_content;

    unsigned int owner_id = page_content->owner_id;
    dsm_node_content *owner_node = NULL;

    dsm_hash_safe_get(&dsm_ctx->nodes_reg, node_content, node, owner_id) {
        if (node_content->id == owner_id) {
            owner_node = node_content;
        }
    }
    dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, owner_id);

    if (!owner_node) {
        LOG_ERROR("Erreur Algo : Noeud proprio de la page non trouvé ?!");
        return -1;
    }

    unsigned int cb_id = req_content->header.id;
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

    bool cb_is_reader = false;
    int inva_ack_count = 0;
    dsm_reader_info *pos, *n;
    pthread_mutex_lock(&page_content->head_req_ctx.lock);
    dsm_safe_list_for_each_entry_safe(&page_content->readers, pos, n, node) {

        if (pos->worker_id == cb_id) {
            cb_is_reader = true;

        } else {
            dsm_node_content *reader_node;
            bool found = false;
            int ret = 0;
            dsm_hash_safe_get(&dsm_ctx->nodes_reg, reader_node, node, pos->worker_id) {

                if (!found && reader_node->id == pos->worker_id) {

                    pthread_mutex_lock(&reader_node->send_lock);
                    ret = send_mgr_inva_req(page_content->page_id, true,
                                            reader_node->tcp_sock);

                    inva_ack_count++;
                    pthread_mutex_unlock(&reader_node->send_lock);
                    found = true;
                }
            }
            dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, pos->worker_id);

            if (ret < 0) {
                pthread_mutex_unlock(&page_content->head_req_ctx.lock);
                return -1;
            }

            if (!found) {
                pthread_mutex_unlock(&page_content->head_req_ctx.lock);
                LOG_ERROR("Erreur Algo : Noeud lecteur de la page non trouvé ?!");
                return -1;
            }
        }

        dsm_list_del(&pos->node);
        free(pos);
    }

    page_content->mode = PAGE_LOCKED;
    page_content->owner_id = cb_id;

    int ret = 0;
    if (cb_is_reader || cb_id == owner_id) {

        if (cb_is_reader) {

            pthread_mutex_lock(&owner_node->send_lock);
            ret = send_mgr_inva_req(page_content->page_id, true, owner_node->tcp_sock);
            pthread_mutex_unlock(&owner_node->send_lock);

            inva_ack_count++;

            if (ret < 0) {
                pthread_mutex_unlock(&page_content->head_req_ctx.lock);
                return -1;
            }
        }

        page_content->head_req_ctx.missing_acks = inva_ack_count;
        page_content->head_req_ctx.promotion_only = true;

    } else {

        pthread_mutex_lock(&owner_node->send_lock);
        ret = send_mgr_data_req(page_content->page_id, true, owner_node->tcp_sock);
        pthread_mutex_unlock(&owner_node->send_lock);

        if (ret < 0) {
            pthread_mutex_unlock(&page_content->head_req_ctx.lock);
            return -1;
        }

        page_content->head_req_ctx.missing_acks = 1 + inva_ack_count;
    }

    *req_completed = false;

    pthread_mutex_unlock(&page_content->head_req_ctx.lock);

    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_REQ);
    return 0;
}

int treat_lock_req_poss_rw(dsm_page_content *page_content,
                           dsm_request_content *req_content, bool *req_completed) {

    LOG_INFO("Traitement d'un CODE=%d ... | LOCK_REQ sur PAGE_RW", CODE_WKR_REQ);

    dsm_node_content *node_content;

    unsigned int owner_id = page_content->owner_id;
    dsm_node_content *owner_node = NULL;

    dsm_hash_safe_get(&dsm_ctx->nodes_reg, node_content, node, owner_id) {
        if (node_content->id == owner_id) {
            owner_node = node_content;
        }
    }
    dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, owner_id);

    if (!owner_node) {
        LOG_ERROR("Erreur Algo : Noeud proprio de la page non trouvé ?!");
        return -1;
    }

    unsigned int cb_id = req_content->header.id;
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

    page_content->mode = PAGE_LOCKED;
    page_content->owner_id = cb_id;

    int ret = 0;

    if (cb_id == owner_id) {

        pthread_mutex_lock(&cb_node->send_lock);
        ret = send_mgr_req_ack(page_content->page_id, req_content->info.mode,
                               PROMOTE_ONLY, NULL, cb_node->tcp_sock);
        pthread_mutex_unlock(&cb_node->send_lock);

        if (ret < 0) {
            return -1;
        }

        // *req_completed = true;

    } else {
        pthread_mutex_lock(&page_content->head_req_ctx.lock);

        pthread_mutex_lock(&owner_node->send_lock);
        ret = send_mgr_data_req(page_content->page_id, true, owner_node->tcp_sock);
        pthread_mutex_unlock(&owner_node->send_lock);

        if (ret < 0) {
            pthread_mutex_unlock(&page_content->head_req_ctx.lock);
            return -1;
        }

        page_content->head_req_ctx.missing_acks = 1;

        // *req_completed = false;

        pthread_mutex_unlock(&page_content->head_req_ctx.lock);
    }

    *req_completed = false;
    LOG_INFO("Succès du traitement du CODE=%d", CODE_WKR_REQ);
    return 0;
}

int send_sync_barr_ack(int sock) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_SYNC_BARRIER_ACK);

    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_sync_barrier_ack(msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(sock, msg_buf, msg_len);
    if (sent < 0) {
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_SYNC_BARRIER_ACK);
    return 0;
}

int send_sync_fina_ack(int sock) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_SYNC_FINALIZE_ACK);

    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_sync_fina_ack(msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(sock, msg_buf, msg_len);
    if (sent < 0) {
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_SYNC_FINALIZE_ACK);
    return 0;
}