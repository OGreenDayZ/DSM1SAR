#include "pager_msg.h"

#include <stdint.h>

#include "debug/debug.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "pager/pager.h"

int send_wkr_req(uint32_t page_id, uint8_t mode) {
    LOG_INFO("Envoi du CODE=%d ...", CODE_WKR_REQ);
    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_wkr_req(page_id, mode, msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;

    return 0;
}

int send_mgr_inva(uint32_t page_id, uint8_t isSync) {
    LOG_INFO("Envoi du CODE=%d ...", CODE_PAGER_INVALIDE);
    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_mgr_inva_req(page_id, isSync, msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;
    return 0;
}

int send_mgr_data_req(uint32_t page_id, uint8_t flags) {
    LOG_INFO("Envoi du CODE=%d ...", CODE_MGR_DATA_REQ);
    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_mgr_data_req(page_id, flags, msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;
    return 0;
}

// TODO : check if data == null
int send_wkr_data(uint32_t page_id, uint8_t *data) {
    LOG_INFO("Envoi du CODE=%d ...", CODE_WKR_DATA_ACK);
    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_data_ack(CODE_WKR_DATA_ACK, page_id, data, EFFECTIVE_PAGESIZE,
                                     msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;
    return 0;
}

int send_mgr_data(uint32_t page_id, uint8_t *data) {
    LOG_INFO("Envoi du CODE=%d ...", CODE_MGR_DATA_ACK);
    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_data_ack(CODE_MGR_DATA_ACK, page_id, data, EFFECTIVE_PAGESIZE,
                                     msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;
    return 0;
}

int send_mgr_ack(uint32_t page_id, uint32_t mode, uint8_t flags, uint8_t *data) {
    LOG_INFO("Envoi du CODE=%d ...", CODE_PAGER_MANAGER_ACK);
    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_pgr_mgr_ack(page_id, mode, flags, data, EFFECTIVE_PAGESIZE,
                                        msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;
    return 0;
}
