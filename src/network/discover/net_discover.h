#ifndef DSM_NET_DISCOVER_H
#define DSM_NET_DISCOVER_H

#include "dsm.h"
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdint.h>

#define DSM_DSCV_MAX_RETRIES 3
#define DSM_DSCV_RESEND_INTERVAL_SEC 2

/* Uniquemement utile jusqu'à la fin du discover */
typedef struct {
    uint32_t my_ip;

    int sock_tcp_listen;
    int sock_udp_recv;
    int sock_udp_mcast;
    uint32_t multicast_ip;

    uint32_t supervisor_ip;
    uint16_t supervisor_tcp_port;
    int supervisor_tcp_sock;
    dsm_node_role supervisor_role;

} dsm_dscv_ctx;

int dsm_discover(void);

#endif // DSM_NET_DISCOVER_H