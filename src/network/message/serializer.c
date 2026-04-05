#include "serializer.h"
#include "network/message/message.h"
#include <string.h>

int static serialize_header(dsm_msg_header_t *header, uint8_t *buf, size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header_t)) {
        return -1;
    }
    int len = sizeof(dsm_msg_header_t);
    header->code = htonl(header->code);
    header->id = htonl(header->id);
    header->payload_len = htonl(header->payload_len);
    memcpy(buf, header, len);
    return len;
}

int dsm_srlze_dscv_init(dsm_dscv_ctx_t *dscv_ctx, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_spvs_data(dsm_dscv_ctx_t *dscv_ctx, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_join_ack(dsm_dscv_ctx_t *dscv_ctx, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_sys_full(dsm_dscv_ctx_t *dscv_ctx, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_share_reg(dsm_dscv_ctx_t *dscv_ctx, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_share_reg_ack(dsm_dscv_ctx_t *dscv_ctx, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_mgr_ready(dsm_dscv_ctx_t *dscv_ctx, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_wkr_config(dsm_dscv_ctx_t *dscv_ctx, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_wkr_config_ack(dsm_dscv_ctx_t *dscv_ctx, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_finalize(dsm_dscv_ctx_t *dscv_ctx, uint8_t *buf, size_t buf_sz);