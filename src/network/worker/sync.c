#include "sync.h"
#include "debug/debug.h"
#include "internal.h"
#include "network/daemon.h"
#include "network/message/serializer.h"
#include "network/worker/worker.h"

int send_sync_barr_req(int sock) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_SYNC_BARRIER_REQ);

    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_sync_barrier_req(msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(sock, msg_buf, msg_len);
    if (sent < 0) {
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_SYNC_BARRIER_REQ);
    return 0;
}

int send_sync_fina_req(int sock) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_SYNC_FINALIZE_REQ);

    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_sync_fina_req(msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(sock, msg_buf, msg_len);
    if (sent < 0) {
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_SYNC_FINALIZE_REQ);
    return 0;
}

int barrier_init(sync_barrier *barr) {
    pthread_mutex_init(&barr->lock, NULL);
    pthread_cond_init(&barr->wait_cond, NULL);
    barr->state = BARR_RESET;
    barr->targ_mgr_id = 0; // Par défaut, ca sera toujours le ManagerID=0
    barr->targ_count = 0;
    barr->curr_count = 0;

    return 0;
}

/**
 * @return 0 succès, 1 barrière déjà définie, -1 erreur
 */
int barrier_define(sync_barrier *barr, unsigned int threads_count,
                   unsigned int manager_id) {
    if (barr->state != BARR_RESET) {
        LOG_INFO("Une barrière en cours est déjà définie");
        return 1;
    }

    unsigned int nb_managers = dsm_ctx->config.net.system.nb_managers;
    if (manager_id >= nb_managers) {
        LOG_ERROR("manager_id doit être < nb_managers (%u)", nb_managers);
        return -1;
    }

    if (threads_count == 0) {
        LOG_ERROR("threads_count doit être > 0");
        return -1;
    }

    barr->targ_mgr_id = manager_id;
    barr->curr_count = 0;
    barr->targ_count = threads_count;
    barr->state = BARR_WAITING;

    LOG_INFO("Barrière définie pour manager_id=%u avec %u threads", manager_id,
             threads_count);

    return 0;
}

int barrier_wait(sync_barrier *barr) {
    if (barr->state != BARR_WAITING) {
        LOG_ERROR("Aucune barrière n'est définie");
        return -1;
    }

    if (barr->targ_count == barr->curr_count) {
        LOG_ERROR("Barrière au complet, impossible d'attendre");
        return -1;
    }

    barr->curr_count++;

    unsigned int mgr_id = barr->targ_mgr_id;
    if (barr->curr_count == barr->targ_count) {
        LOG_INFO("Tous les threads sont à la barrière, envoi du signal au manager_id=%u",
                 mgr_id);
        dsm_mgr_to_node_content *content;
        dsm_hash_safe_get(&dsm_wkr_ctx->managerID_to_nodeID, content, node, mgr_id) {
            if (content->manager_id == mgr_id) {
                pthread_mutex_lock(&content->to_node_content->send_lock);
                int ret = send_sync_barr_req(content->to_node_content->tcp_sock);
                pthread_mutex_unlock(&content->to_node_content->send_lock);
                if (ret < 0) {
                    dsm_hash_safe_unlock(&dsm_wkr_ctx->managerID_to_nodeID, mgr_id);
                    return -1;
                }
            }
        }
        dsm_hash_safe_get_end(&dsm_wkr_ctx->managerID_to_nodeID, mgr_id);
    }

    LOG_INFO("[%d/%d] Thread %lu en attente à la barrière pour manager_id=%u...",
             barr->curr_count, barr->targ_count, (unsigned long)pthread_self(), mgr_id);
    while (barr->state != BARR_TRIGGERED) {
        pthread_cond_wait(&barr->wait_cond, &barr->lock);
    }

    barr->curr_count--;
    LOG_INFO("Fin de la barrière");

    if (barr->curr_count == 0) {
        LOG_INFO(
            "Dernier thread à sortir de la barrière, réinitialisation de la barrière");
        barr->state = BARR_RESET;
    }

    return 0;
}

int barrier_trigger(sync_barrier *barr) {
    if (barr->state != BARR_WAITING) {
        LOG_ERROR("Aucune barrière n'est définie ou barrière déjà déclenchée");
        return -1;
    }

    barr->state = BARR_TRIGGERED;
    pthread_cond_broadcast(&barr->wait_cond);
    LOG_INFO("Barrière déclenchée, tous les threads en attente seront réveillés");

    return 0;
}

int finalize_init(sync_finalize *fina) {
    pthread_mutex_init(&fina->lock, NULL);
    pthread_cond_init(&fina->wait_cond, NULL);
    fina->state = FINA_RESET;
    fina->targ_count = 0;
    fina->curr_count = 0;
    fina->missing_mgr_acks = dsm_ctx->config.net.system.nb_managers;

    return 0;
}

int finalize_define(sync_finalize *fina, unsigned int threads_count) {
    if (fina->state != FINA_RESET) {
        LOG_INFO("Une finalisation est déjà en cours");
        return 1;
    }

    if (threads_count == 0) {
        LOG_ERROR("threads_count doit être > 0");
        return -1;
    }

    fina->curr_count = 0;
    fina->targ_count = threads_count;
    fina->state = FINA_WAITING;

    LOG_INFO("Finalisation définie pour %u threads", threads_count);

    return 0;
}

int finalize_wait(sync_finalize *fina) {
    if (fina->state != FINA_WAITING) {
        LOG_ERROR("Aucune finalisation n'est définie");
        return -1;
    }

    if (fina->targ_count == fina->curr_count) {
        LOG_ERROR("Finalisation au complet, impossible d'attendre");
        return -1;
    }

    fina->curr_count++;

    if (fina->curr_count == fina->targ_count) {
        LOG_INFO("Tous les threads sont à la finalisation, envoi des demandes de "
                 "finalisation aux managers");
        unsigned int bkt;
        dsm_node_content *pos;
        dsm_hash_safe_for_each(&dsm_ctx->nodes_reg, bkt, pos, node) {
            pthread_mutex_lock(&pos->send_lock);
            int ret = send_sync_fina_req(pos->tcp_sock);
            pthread_mutex_unlock(&pos->send_lock);
            if (ret < 0) {
                dsm_hash_safe_unlock(&dsm_ctx->nodes_reg, bkt);
                return -1;
            }
        }
    }

    LOG_INFO("[%d/%d] Thread %lu : en attente pour la finalisation", fina->curr_count,
             fina->targ_count, (unsigned long)pthread_self());
    while (fina->state != FINA_TRIGGERED) {
        pthread_cond_wait(&fina->wait_cond, &fina->lock);
    }

    // PAS DE RESET CAR FINALISATION UNIQUE

    LOG_INFO("Thread %lu : Finalisation permise", (unsigned long)pthread_self());

    return 0;
}

int finalize_trigger(sync_finalize *fina) {
    if (fina->state != FINA_WAITING) {
        LOG_ERROR("Aucune finalisation n'est définie ou finalisation déjà déclenchée");
        return -1;
    }

    fina->missing_mgr_acks--;
    LOG_INFO("[%d/%d] ACK de finalisation reçu",
             dsm_ctx->config.net.system.nb_managers - fina->missing_mgr_acks,
             dsm_ctx->config.net.system.nb_managers);

    if (fina->missing_mgr_acks == 0) {
        fina->state = FINA_TRIGGERED;
        atomic_store(&dsm_dmn_ctx->state, DSM_DMN_STATE_TERMINATING);
        pthread_cond_broadcast(&fina->wait_cond);
        LOG_INFO("Finalisation déclenchée, tous les threads en attente seront réveillés");
    }

    return 0;
}