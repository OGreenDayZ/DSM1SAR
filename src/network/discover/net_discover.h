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

    atomic_int id_count;
    // JOIN LIST ?
    int tcp_port_connect_spvs; // Le port que le worker utilise pour se connecter au
                               // superviseur, à communiquer dans le message d'init
} dsm_dscv_ctx;

int dsm_discover(void);

#endif // DSM_NET_DISCOVER_H