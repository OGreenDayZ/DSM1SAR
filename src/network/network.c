#include "network.h"

#include <ifaddrs.h>
#include <net/if.h>
#include <string.h>
#include <unistd.h>

#include "debug/debug.h"
#include "network/discover/net_discover.h"

int dsm_get_local_ip(uint32_t *ip) {
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        LOG_ERROR("getifaddrs");
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        if (ifa->ifa_flags & IFF_LOOPBACK) {
            continue;
        }

        if (!(ifa->ifa_flags & IFF_UP)) {
            continue;
        }

        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        *ip = ntohl(addr->sin_addr.s_addr);
        break;
    }

    freeifaddrs(ifaddr);

    if (ifa == NULL) {
        LOG_ERROR("Aucune interface IPv4 locale valide trouvée.");
        return -1;
    }
    return 0;
}

int dsm_make_addr(uint32_t ip, uint16_t port, struct sockaddr_in *addr) {
    memset(addr, 0, sizeof(struct sockaddr_in));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);

    if (ip == 0) {
        addr->sin_addr.s_addr = INADDR_ANY;
    } else {
        addr->sin_addr.s_addr = htonl(ip);
    }

    return 0;
}

int dsm_tcp_sock() {
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERROR("socket TCP");
        return -1;
    }

    int ok = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &ok, sizeof(ok)) < 0 ||
        setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &ok, sizeof(ok)) < 0) {
        LOG_ERROR("setsockopt SO_REUSEADDR");
        close(sock);
        return -1;
    }

    return sock;
}

int dsm_tcp_connect_sock(uint32_t ip, uint16_t port) {
    int sock = dsm_tcp_sock();
    if (sock < 0)
        return -1;

    struct sockaddr_in addr;
    if (dsm_make_addr(ip, port, &addr) < 0) {
        goto error_exit;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("connect");
        goto error_exit;
    }

    return sock;

error_exit:
    close(sock);
    return -1;
}

int dsm_tcp_bind_listen(uint16_t port) {
    int sock = dsm_tcp_sock();
    if (sock < 0)
        return -1;

    struct sockaddr_in addr;
    if (dsm_make_addr(0, port, &addr) < 0) {
        goto error_exit;
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind");
        goto error_exit;
    }

    if (listen(sock, SOMAXCONN) < 0) {
        LOG_ERROR("listen");
        goto error_exit;
    }

    return sock;

error_exit:
    close(sock);
    return -1;
}

int dsm_udp_sock() {
    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOG_ERROR("socket UDP");
        return -1;
    }

    int ok = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &ok, sizeof(ok)) < 0 ||
        setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &ok, sizeof(ok)) < 0) {
        LOG_ERROR("setsockopt SO_REUSEADDR");
        close(sock);
        return -1;
    }

    return sock;
}

int dsm_udp_bind_unicast(uint16_t port) {
    int sock = dsm_udp_sock();
    if (sock < 0)
        return -1;

    struct sockaddr_in addr;
    if (dsm_make_addr(0, port, &addr) < 0) {
        goto error_exit;
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind unicast");
        goto error_exit;
    }

    return sock;

error_exit:
    close(sock);
    return -1;
}

int dsm_udp_bind_multicast(uint32_t mcast_ip, uint16_t port) {
    int sock = dsm_udp_bind_unicast(port);
    if (sock < 0)
        return -1;

    struct ip_mreq group;
    memset(&group, 0, sizeof(group));
    group.imr_multiaddr.s_addr = htonl(mcast_ip);
    group.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof(group)) < 0) {
        LOG_ERROR("IP_ADD_MEMBERSHIP");
        goto error_exit;
    }

    // uint8_t loop = 0;
    // if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
    //     perror("setsockopt loopback");
    // }

    return sock;

error_exit:
    close(sock);
    return -1;
}

int dsm_network_init() {
    return dsm_discover();
}
