#ifndef DSM_SERIALIZER_H
#define DSM_SERIALIZER_H

#include "network/message/message.h"

/* Lexique
@srlze: serialize
@dsrlze: deserialize
*/
// DISCOVER :
int dsm_srlze_dscv_init(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_spvs_data(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_join_ack(int assigned_id, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_sys_full(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_role_wish(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_share_reg(uint32_t code, dsm_msg_dscv_share_reg *share, uint8_t *buf,
                             size_t buf_sz);
int dsm_srlze_dscv_share_reg_ack(uint16_t listen_port, uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_mgr_ready(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_wkr_config(uint64_t nb_pages, uint32_t page_size, uint8_t *buf,
                              size_t buf_sz);
int dsm_srlze_dscv_wkr_config_ack(uint8_t *buf, size_t buf_sz);
int dsm_srlze_dscv_finalize(uint8_t *buf, size_t buf_sz);

// PAGER : 
int dsm_srlze_pgr_wkr_req(uint32_t page_id, uint32_t worker_id, uint8_t mode,uint8_t *buf, size_t buf_sz); 
int dsm_srlze_pgr_mgr_inva(uint32_t page_id, uint8_t *buf, size_t buf_sz); 
int dsm_srlze_pgr_mgr_data_req(uint32_t page_id, uint8_t *buf, size_t buf_sz); 
int dsm_srlze_pgr_data(uint32_t page_id, uint8_t *data, size_t data_len, uint8_t *buf,size_t buf_sz); 
/*
@note : Pas de fonction de désérialisation pour les messages sans payload,
        le dsm_dsrlze_header suffit
*/
// DISCOVER : 
int dsm_dsrlze_header(uint8_t *buf, size_t buf_sz, dsm_msg_header *header);
int dsm_dsrlze_dscv_init(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_init *msg);
int dsm_dsrlze_dscv_spvs_data(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_spvs_data *msg);
int dsm_dsrlze_dscv_join_ack(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_join_ack *msg);
int dsm_dsrlze_dscv_role_wish(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_role_wish *msg);
int dsm_dsrlze_dscv_share_reg(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_share_reg *msg);
int dsm_dsrlze_dscv_share_reg_ack(uint8_t *buf, size_t buf_sz,
                                  dsm_msg_dscv_share_reg_ack *msg);
int dsm_dsrlze_dscv_wkr_config(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_wkr_config *msg);
// PAGER : 
int dsm_dsrlze_pgr_wkr_req(uint8_t *buf, size_t buf_sz, dsm_msg_pgr_wkr_req *msg) ; 
int dsm_dsrlze_pgr_mgr_inva(uint8_t *buf, size_t buf_sz, dsm_msg_pgr_mgr_inval *msg); 
int dsm_dsrlze_pgr_mgr_data_req(uint8_t *buf, size_t buf_sz, dsm_msg_pgr_mgr_data_req *msg); 
int dsm_dsrlze_pgr_data(uint8_t *buf, size_t buf_sz, dsm_msg_pgr_data *msg);

#endif // DSM_SERIALIZER_H