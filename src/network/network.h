#ifndef DSM_NETWORK_H
#define DSM_NETWORK_H

#include <netinet/in.h>

#define DSM_DEFAULT_MULTICAST_IP "239.1.2.3"
#define DSM_DEFAULT_MULTICAST_PORT 54321

int dsm_get_local_ip(uint32_t *ip);
int dsm_make_addr(uint32_t ip, uint16_t port, struct sockaddr_in *addr);
int dsm_tcp_sock(void);
int dsm_tcp_connect_sock(uint32_t ip, uint16_t port);
int dsm_tcp_bind_listen(uint16_t port);
int dsm_udp_sock(void);
int dsm_udp_bind_unicast(uint16_t port);
int dsm_udp_bind_multicast(uint32_t mcast_ip, uint16_t port);

#endif // DSM_NETWORK_H