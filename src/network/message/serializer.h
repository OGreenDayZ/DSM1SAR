#ifndef DSM_SERIALIZER_H
#define DSM_SERIALIZER_H

#include "network/discover/net_discover.h"

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

int dsm_recv_and_dispatch(/* TODO */);

#endif // DSM_SERIALIZER_H