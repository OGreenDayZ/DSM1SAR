#ifndef DSM_NET_DISCOVER_H
#define DSM_NET_DISCOVER_H

#include <netinet/in.h>
#include <stdbool.h>

typedef struct {
    char my_ip[INET_ADDRSTRLEN];
    int sock_tcp_listen;
    int sock_udp_recv;
    int sock_mcast_udp;

    bool is_supervisor;

    int id_count;
    // JOIN LIST ?
} dsm_dscv_ctx_t;

int dsm_discover(void);

#endif // DSM_NET_DISCOVER_H