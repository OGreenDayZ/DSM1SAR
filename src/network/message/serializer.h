#ifndef DSM_SERIALIZER_H
#define DSM_SERIALIZER_H

#include "network/message/message.h"
#include <stdint.h>

/** Lexique
 * @srlze: serialize
 * @dsrlze: deserialize
 */

int dsm_srlze_dscv_init(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_spvs_data(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_join_ack(int assigned_id, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_sys_full(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_role_wish(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_share_reg(uint32_t code, dsm_msg_dscv_share_reg *share, uint8_t *buf,
                             size_t buf_sz);
int dsm_srlze_dscv_share_reg_ack(uint16_t listen_port, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_wkr_id(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_wkr_id_ack(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_mgr_ready(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_wkr_config(uint64_t nb_pages, uint32_t page_size, uint8_t *buf,
                              size_t buf_sz);
int dsm_srlze_dscv_wkr_config_ack(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_finalize(uint8_t *buf, size_t buf_sz);

// PAGER :
int dsm_srlze_wkr_req(uint32_t page_id, uint8_t mode, uint8_t *buf, size_t buf_sz);
int dsm_srlze_mgr_inva_req(uint32_t page_id, uint8_t is_sync, uint8_t *buf,
                           size_t buf_sz);
int dsm_srlze_mgr_data_req(uint32_t page_id, uint8_t is_invalidate, uint8_t *buf,
                           size_t buf_sz);
int dsm_srlze_wkr_inva_ack(uint32_t page_id, uint8_t *buf, size_t buf_sz);
int dsm_srlze_wkr_data_ack(uint32_t page_id, uint8_t *data, uint8_t *buf, size_t buf_sz);
int dsm_srlze_mgr_req_ack(uint32_t page_id, uint8_t req_mode, uint8_t is_empty,
                          uint8_t *data, uint8_t *buf, size_t buf_sz);
/**
 * @note: Pas de fonction de désérialisation pour les messages sans payload,
 *        le dsm_dsrlze_header suffit
 */
int dsm_dsrlze_header(uint8_t *buf, size_t buf_sz, dsm_msg_header *header);
int dsm_dsrlze_dscv_init(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_init *msg);
int dsm_dsrlze_dscv_spvs_data(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_spvs_data *msg);
int dsm_dsrlze_dscv_join_ack(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_join_ack *msg);
int dsm_dsrlze_dscv_role_wish(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_role_wish *msg);
int dsm_dsrlze_dscv_share_reg(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_share_reg **msg);
int dsm_dsrlze_dscv_share_reg_ack(uint8_t *buf, size_t buf_sz,
                                  dsm_msg_dscv_share_reg_ack *msg);
int dsm_dsrlze_dscv_wkr_config(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_wkr_config *msg);

// PAGER :
int dsm_dsrlze_wkr_req(uint8_t *buf, size_t buf_sz, dsm_msg_wkr_req *msg);
int dsm_dsrlze_mgr_inva_req(uint8_t *buf, size_t buf_sz, dsm_msg_mgr_inva_req *msg);
int dsm_dsrlze_mgr_data_req(uint8_t *buf, size_t buf_sz, dsm_msg_mgr_data_req *msg);
int dsm_dsrlze_wkr_inva_ack(uint8_t *buf, size_t buf_sz, dsm_msg_wkr_inva_ack *msg);
int dsm_dsrlze_wkr_data_ack(uint8_t *buf, size_t buf_sz, dsm_msg_wkr_data_ack **msg);
int dsm_dsrlze_mgr_req_ack(uint8_t *buf, size_t buf_sz, dsm_msg_mgr_req_ack **msg);
#endif // DSM_SERIALIZER_H