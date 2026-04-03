#ifndef DSM_NETWORK_H
#define DSM_NETWORK_H

#include <netinet/in.h>
#include <stddef.h>

#define DSM_MULTICAST_IP "239.1.2.3"
#define DSM_MULTICAST_PORT 54321

int dsm_get_local_ip(char *buffer, size_t buflen);
int dsm_make_addr(const char *ip, int port, struct sockaddr_in *addr);
int dsm_tcp_sock(void);
int dsm_tcp_connect_sock(const char *ip, int port);
int dsm_tcp_bind_listen(const char *ip, int port);
int dsm_udp_sock(void);
int dsm_udp_bind_unicast(const char *ip, int port);
int dsm_udp_bind_multicast(const char *mcast_addr, int port);

int dsm_network_init(void);

#endif // DSM_NETWORK_H