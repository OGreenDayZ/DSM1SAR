#ifndef DSM_NET_DISCOVER_H
#define DSM_NET_DISCOVER_H

#include <netinet/in.h>
#include <stdatomic.h>
#include <stdbool.h>

#define DSM_DSCV_MAX_RETRIES 3
#define DSM_DSCV_RESEND_INTERVAL_SEC 2

typedef struct {
    char my_ip[INET_ADDRSTRLEN];
    int sock_tcp_listen;
    int sock_udp_recv;
    int sock_mcast_udp;

    bool is_supervisor;

    char supervisor_ip[INET_ADDRSTRLEN];
    uint16_t supervisor_tcp_port;

    atomic_int id_count;
    // JOIN LIST ?

} dsm_dscv_ctx;

int dsm_discover(void);

#endif // DSM_NET_DISCOVER_H