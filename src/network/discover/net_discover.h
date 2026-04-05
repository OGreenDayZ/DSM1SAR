#ifndef DSM_NET_DISCOVER_H
#define DSM_NET_DISCOVER_H

#include <netinet/in.h>
#include <stdatomic.h>
#include <stdbool.h>

#define DSM_DISCOVER_UDP_MAX_RETRIES 3
#define DSM_DISCOVER_UDP_RESEND_INTERVAL_SEC 2

typedef struct {
    char my_ip[INET_ADDRSTRLEN];
    int sock_tcp_listen;
    int sock_udp_recv;
    int sock_mcast_udp;

    bool is_supervisor;

    atomic_int id_count;
    // JOIN LIST ?
} dsm_dscv_ctx_t;

// int dsm_send_dscv_init(dsm_dscv_ctx_t *ctx);
// int dsm_send_dscv_spvs_data(dsm_dscv_ctx_t *ctx);
// int dsm_send_dscv_join_ack(dsm_dscv_ctx_t *ctx);
// int dsm_send_dscv_sys_full(dsm_dscv_ctx_t *ctx);
// int dsm_send_dscv_share_reg(dsm_dscv_ctx_t *ctx);
// int dsm_send_dscv_share_reg_ack(dsm_dscv_ctx_t *ctx);
// int dsm_send_dscv_mgr_ready(dsm_dscv_ctx_t *ctx);
// int dsm_send_dscv_wkr_config(dsm_dscv_ctx_t *ctx);
// int dsm_send_dscv_wkr_config_ack(dsm_dscv_ctx_t *ctx);
// int dsm_send_dscv_finalize(dsm_dscv_ctx_t *ctx);

int dsm_discover(void);

#endif // DSM_NET_DISCOVER_H