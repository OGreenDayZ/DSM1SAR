#include "net_discover.h"

#include "internal.h"
#include "network/network.h"
#include <unistd.h>

static dsm_dscv_ctx_t discover_ctx;

static int discover_sockets_init() {
    int ret = dsm_get_local_ip(discover_ctx.my_ip, INET_ADDRSTRLEN);
    if (ret < 0) {
        return -1;
    }

    discover_ctx.sock_udp_recv =
        dsm_udp_bind_unicast(discover_ctx.my_ip, dsm_ctx->config.net.node.udp_port);
    if (discover_ctx.sock_udp_recv < 0) {
        return -1;
    }

    discover_ctx.sock_mcast_udp =
        dsm_udp_bind_multicast(dsm_ctx->config.net.system.multicast_ip,
                               dsm_ctx->config.net.system.multicast_port);

    if (discover_ctx.sock_mcast_udp < 0) {
        close(discover_ctx.sock_udp_recv);
        return -1;
    }

    return 0;
}

static int discover_scan_network() {
    return 0;
}

int dsm_discover() {

    if (discover_sockets_init() < 0) {
        return -1;
    }

    if (discover_scan_network() < 0) {
        return -1;
    }

    return 0;
}