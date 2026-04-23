#ifndef DSM_PAGER_MSG_H
#define DSM_PAGER_MSG_H

#include <stdint.h>

int send_wkr_req(uint32_t page_id, uint8_t mode); 
int send_mgr_inva(uint32_t page_id, uint8_t isSync); 
int send_mgr_data_req(uint32_t page_id, uint8_t flags); 
int send_pgr_data(uint32_t page_id, uint8_t *data); 
int send_mgr_ack(uint32_t page_id, uint32_t mode, uint8_t flags, uint8_t *data); 

#endif // DSM_PAGER_MSG_H