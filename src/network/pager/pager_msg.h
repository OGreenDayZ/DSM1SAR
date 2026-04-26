#ifndef DSM_PAGER_MSG_H
#define DSM_PAGER_MSG_H

#include <stdint.h>

int send_wkr_req(uint32_t page_id, uint8_t mode, int fd_sock);  // OK
int send_mgr_inva_req(uint32_t page_id, uint8_t is_sync,int fd_sock); // OK
int send_mgr_data_req(uint32_t page_id, uint8_t is_invalidate, int fd_sock); // OK
int send_wkr_inva_ack(uint32_t page_id, int fd_sock); // OK
int send_wkr_data_ack(uint32_t page_id, uint8_t* data, int fd_sock);  // OK
int send_mgr_data_ack(uint32_t page_id, uint8_t req_mode, uint8_t is_empty, uint8_t* data, int fd_sock); // OK
int send_mgr_lock_ack(uint32_t page_id, uint8_t is_empty, uint8_t* data, int fd_sock); // OK
#endif // DSM_PAGER_MSG_H