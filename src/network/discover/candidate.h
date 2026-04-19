#ifndef DSM_CANDIDATE_H
#define DSM_CANDIDATE_H

#include "dsm.h"
#include "network/discover/net_discover.h"
#include "util/concurrent/safe_map.h"

typedef struct {
    unsigned int id;
    int tcp_sock;
    uint32_t ip;
    struct dsm_hlist_node node;
} dsm_cand_content; // Dans cand_reg

typedef struct {
    uint32_t supervisor_ip;
    uint16_t supervisor_tcp_port;
    int supervisor_tcp_sock;
    dsm_node_role supervisor_role;

    dsm_safe_map cand_reg;
} dsm_candidate_ctx;

int dsm_candidate_start(dsm_dscv_ctx *dscv_ctx);

#endif // DSM_CANDIDATE_H