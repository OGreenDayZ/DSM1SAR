#ifndef DSM_CANDIDATE_H
#define DSM_CANDIDATE_H

#include <stdint.h>

#include "dsm.h"
#include "network/discover/net_discover.h"
#include "network/worker/worker.h"
#include "util/concurrent/safe_map.h"
#include "util/hlist.h"

#define DSM_CAND_HASH_BITS 4

typedef struct {
    unsigned int id;
    int tcp_sock;
    uint32_t ip;
    /**
     * Stocké lors du connect_to_managers
     * Utilisé lors du transfert de sockets : cand_ctx_cleanup
     * @note : Simplifie le transfert
     */
    dsm_mgr_to_node_content *set_shortcut_mgr;
    struct dsm_hlist_node node;
} dsm_cand_content; // Dans cand_reg

typedef struct {
    uint32_t supervisor_ip;
    uint16_t supervisor_tcp_port;
    int supervisor_tcp_sock;
    dsm_node_role supervisor_role;

    dsm_safe_map cand_reg;
} dsm_candidate_ctx;

int dsm_candidate_start(dsm_discover_ctx *dscv_ctx, uint32_t spvs_ip,
                        uint16_t spvs_tcp_port);

#endif // DSM_CANDIDATE_H