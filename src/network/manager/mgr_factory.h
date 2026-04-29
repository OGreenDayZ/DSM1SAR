#ifndef DSM_MGR_FACTORY_H
#define DSM_MGR_FACTORY_H

/**
 * @file : sous fichier de manager.c
 */

#include "network/manager/manager.h"

/**
 * @brief Traite une READ_REQ lorsque le mode_poss est PAGE_INIT
 */
int treat_read_req_poss_init(dsm_page_content *page_content,
                             dsm_request_content *req_content, bool *req_completed);

/**
 * @brief Traite une READ_REQ lorsque le mode_poss est PAGE_R
 */
int treat_read_req_poss_r(dsm_page_content *page_content,
                          dsm_request_content *req_content, bool *req_completed);

/**
 * @brief Traite une READ_REQ lorsque le mode_poss est PAGE_RW
 */
int treat_read_req_poss_rw(dsm_page_content *page_content,
                           dsm_request_content *req_content, bool *req_completed);

/******************/

/**
 * @brief Traite une WRITE_REQ lorsque le mode_poss est PAGE_INIT
 */
int treat_write_req_poss_init(dsm_page_content *page_content,
                              dsm_request_content *req_content, bool *req_completed);

/**
 * @brief Traite une WRITE_REQ lorsque le mode_poss est PAGE_R
 */
int treat_write_req_poss_r(dsm_page_content *page_content,
                           dsm_request_content *req_content, bool *req_completed);

/**
 * @brief Traite une WRITE_REQ lorsque le mode_poss est PAGE_RW
 */
int treat_write_req_poss_rw(dsm_page_content *page_content,
                            dsm_request_content *req_content, bool *req_completed);

/******************/

/**
 * @brief Traite une LOCK_REQ lorsque le mode_poss est PAGE_INIT
 */
int treat_lock_req_poss_init(dsm_page_content *page_content,
                             dsm_request_content *req_content, bool *req_completed);

/**
 * @brief Traite une LOCK_REQ lorsque le mode_poss est PAGE_R
 */
int treat_lock_req_poss_r(dsm_page_content *page_content,
                          dsm_request_content *req_content, bool *req_completed);

/**
 * @brief Traite une LOCK_REQ lorsque le mode_poss est PAGE_RW
 */
int treat_lock_req_poss_rw(dsm_page_content *page_content,
                           dsm_request_content *req_content, bool *req_completed);

/******************/

#endif // DSM_MGR_FACTORY_H