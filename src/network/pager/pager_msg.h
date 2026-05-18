#ifndef DSM_PAGER_MSG_H
#define DSM_PAGER_MSG_H

#include <stdint.h>

/**
 * @brief Envoi du message code=CODE_WKR_REQ.
 *
 * @param page_id la page concerner par la requete.
 * @param mode le mode de la requete.
 * @param fd_sock le fd du manager a qui envoyé la requete.
 * @return int 0 en cas de succés, -1 sinon.
 */
int send_wkr_req(uint32_t page_id, uint8_t mode, int fd_sock);

/**
 * @brief Envoi du message code=CODE_WKR_INVA_REQ.
 *
 * @param page_id la page concerner par la requete.
 * @param fd_sock le fd du manager a qui envoyé la requete.
 * @return int 0 en cas de succés, -1 sinon.
 */
int send_wkr_inva_ack(uint32_t page_id, int fd_sock);
int send_wkr_data_ack(uint32_t page_id, uint8_t *data, int fd_sock);

int send_mgr_inva_req(uint32_t page_id, uint8_t is_sync, int fd_sock);
int send_mgr_data_req(uint32_t page_id, uint8_t is_invalidate, int fd_sock);
int send_mgr_req_ack(uint32_t page_id, uint8_t req_mode, uint8_t data_info, uint8_t *data,
                     int fd_sock);
#endif // DSM_PAGER_MSG_H