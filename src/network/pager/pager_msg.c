#include "pager_msg.h"

#include "debug/debug.h"
#include "internal.h"
#include "network/message/message.h"
#include "network/message/serializer.h"

#include <stdint.h>

/**
 * @note : les noms de code sont tous court donc ecrire send_"NOMCODE""
 *  send_mgr_inva into send_mgr_inva_req etc...
 * @note : je dis ca car à cette partie là, le suffixe "_req" ou "_ack" est important pr
 * la lisibilite
 */

int send_wkr_req(uint32_t page_id, uint8_t mode, int fd_sock) {
    LOG_INFO("Envoi du CODE=%d à %d...", CODE_WKR_REQ, fd_sock);
    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_wkr_req(page_id, mode, msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;

    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoie du message de requete pour la page %d par le noeud %d",
                  page_id, dsm_ctx->node_id);
        return -1;
    }
    LOG_INFO("Succès de l'envoie du CODE=%d", CODE_WKR_REQ);
    return 0;
}

int send_mgr_inva_req(uint32_t page_id, uint8_t is_sync, int fd_sock) {
    LOG_INFO("Envoi du CODE=%d à %d...", CODE_MGR_INVA_REQ, fd_sock);
    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_mgr_inva_req(page_id, is_sync, msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;
    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoie du message de requete pour la page %d par le noeud %d",
                  page_id, dsm_ctx->node_id);
        return -1;
    }
    LOG_INFO("Succès de l'envoie du CODE=%d", CODE_MGR_INVA_REQ);
    return 0;
}

int send_mgr_data_req(uint32_t page_id, uint8_t is_invalidate, int fd_sock) {
    LOG_INFO("Envoi du CODE=%d à %d...", CODE_MGR_DATA_REQ, fd_sock);

    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len =
        dsm_srlze_mgr_data_req(page_id, is_invalidate, msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;
    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoie du message de requete pour la page %d par le noeud %d",
                  page_id, dsm_ctx->node_id);
        return -1;
    }
    LOG_INFO("Succès de l'envoie du CODE=%d", CODE_MGR_DATA_REQ);
    return 0;
}

int send_wkr_inva_ack(uint32_t page_id, int fd_sock) {
    LOG_INFO("Envoi du CODE=%d à %d...", CODE_WKR_INVA_ACK, fd_sock);
    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_wkr_inva_ack(page_id, msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;
    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoie du message de requete pour la page %d par le noeud %d",
                  page_id, dsm_ctx->node_id);
        return -1;
    }
    LOG_INFO("Succès de l'envoie du CODE=%d", CODE_WKR_INVA_ACK);
    return 0;
}

int send_wkr_data_ack(uint32_t page_id, uint8_t *data, int fd_sock) {
    LOG_INFO("Envoi du CODE=%d à %d...", CODE_WKR_DATA_ACK, fd_sock);
    int data_len = EFFECTIVE_PAGESIZE;
    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_wkr_data_ack) + data_len];
    int msg_len = dsm_srlze_wkr_data_ack(page_id, data, msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;
    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoie du message de requete pour la page %d par le noeud %d",
                  page_id, dsm_ctx->node_id);
        return -1;
    }
    LOG_INFO("Succès de l'envoie du CODE=%d", CODE_WKR_DATA_ACK);
    return 0;
}

int send_mgr_data_ack(uint32_t page_id, uint8_t req_mode, uint8_t is_empty, uint8_t *data,
                      int fd_sock) {
    LOG_INFO("Envoi du CODE=%d à %d...", CODE_MGR_DATA_ACK, fd_sock);
    int data_len = (is_empty ? 0 : EFFECTIVE_PAGESIZE);
    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_mgr_data_ack) + data_len];
    int msg_len = dsm_srlze_mgr_data_ack(page_id, req_mode, is_empty, data, msg_buf,
                                         sizeof(msg_buf));
    if (msg_len < 0)
        return -1;
    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoie du message de requete pour la page %d par le noeud %d",
                  page_id, dsm_ctx->node_id);
        return -1;
    }
    LOG_INFO("Succès de l'envoie du CODE=%d", CODE_MGR_DATA_ACK);
    return 0;
}

int send_mgr_lock_ack(uint32_t page_id, uint8_t is_empty, uint8_t *data, int fd_sock) {
    LOG_INFO("Envoi du CODE=%d à %d...", CODE_MGR_LOCK_ACK, fd_sock);
    int data_len = (is_empty ? 0 : EFFECTIVE_PAGESIZE);
    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_mgr_lock_ack) + data_len];
    int msg_len =
        dsm_srlze_mgr_lock_ack(page_id, is_empty, data, msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;
    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoie du message de requete pour la page %d par le noeud %d",
                  page_id, dsm_ctx->node_id);
        return -1;
    }
    LOG_INFO("Succès de l'envoie du CODE=%d", CODE_MGR_LOCK_ACK);
    return 0;
}