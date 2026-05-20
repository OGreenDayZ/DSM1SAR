#include "pager_msg.h"

#include <stdint.h>

#include "debug/debug.h"
#include "internal.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "pager/pager.h"

int send_wkr_req(uint32_t page_id, uint8_t mode, int fd_sock) {
    LOG_INFO("Envoi d'un CODE=%d ... (mode=%d)", CODE_WKR_REQ, mode);

    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_wkr_req)];
    int msg_len = dsm_srlze_wkr_req(page_id, mode, msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoi du message de requete pour la page %d", page_id);
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_WKR_REQ);
    return 0;
}

int send_wkr_inva_ack(uint32_t page_id, int fd_sock) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_WKR_INVA_ACK);

    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_wkr_inva_ack)];
    int msg_len = dsm_srlze_wkr_inva_ack(page_id, msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoi du message de requete pour la page %d", page_id);
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_WKR_INVA_ACK);
    return 0;
}

/**
 * @note : pas de data_info ici car on demande les données au propriétaire, donc si il a
 * reçu une demande de donnée forcément il a.
 */
int send_wkr_data_ack(uint32_t page_id, uint8_t *data, int fd_sock) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_WKR_DATA_ACK);

    int data_len = EFFECTIVE_PAGESIZE;
    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_wkr_data_ack) + data_len];
    int msg_len = dsm_srlze_wkr_data_ack(page_id, data, msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoi du message de requete pour la page %d", page_id);
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_WKR_DATA_ACK);
    return 0;
}

int send_mgr_inva_req(uint32_t page_id, uint8_t is_sync, int fd_sock) {
    LOG_INFO("Envoi d'un CODE=%d ... (is_sync=%s)", CODE_MGR_INVA_REQ,
             is_sync ? "true" : "false");

    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_mgr_inva_req)];
    int msg_len = dsm_srlze_mgr_inva_req(page_id, is_sync, msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoi du message de requete pour la page %d", page_id);
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_MGR_INVA_REQ);
    return 0;
}

int send_mgr_data_req(uint32_t page_id, uint8_t is_invalidate, int fd_sock) {
    LOG_INFO("Envoi d'un CODE=%d ... (is_invalidate=%s)", CODE_MGR_DATA_REQ,
             is_invalidate ? "true" : "false");

    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_mgr_data_req)];
    int msg_len =
        dsm_srlze_mgr_data_req(page_id, is_invalidate, msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoi du message de requete pour la page %d", page_id);
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_MGR_DATA_REQ);
    return 0;
}

int send_mgr_req_ack(uint32_t page_id, uint8_t req_mode, uint8_t data_info, uint8_t *data,
                     int fd_sock) {
    LOG_INFO("Envoi d'un CODE=%d ... (req_mode=%d, data_info=%d)", CODE_MGR_REQ_ACK,
             req_mode, data_info);

    int data_len = (data_info == NOT_EMPTY ? EFFECTIVE_PAGESIZE : 0);
    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_mgr_req_ack) + data_len];
    int msg_len = dsm_srlze_mgr_req_ack(page_id, req_mode, data_info, data, msg_buf,
                                        sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(fd_sock, msg_buf, msg_len);
    if (sent < 0) {
        LOG_ERROR("Erreur d'envoi du message de requete pour la page %d", page_id);
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_MGR_REQ_ACK);
    return 0;
}
