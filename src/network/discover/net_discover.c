#include "net_discover.h"

#include <netinet/in.h>
#include <stdint.h>
#include <unistd.h>

#include "debug/debug.h"
#include "dsm.h"
#include "internal.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "network/network.h"

static dsm_dscv_ctx discover_ctx;

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

static int send_dscv_init() {
    LOG_INFO("Recherche d'un Superviseur ...\n");
    struct sockaddr_in addr;
    dsm_make_addr(dsm_ctx->config.net.system.multicast_ip,
                  dsm_ctx->config.net.system.multicast_port, &addr);

    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_init)];
    int msg_len = dsm_srlze_dscv_init(msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int send_sock = dsm_udp_sock();
    if (send_sock < 0) {
        return -1;
    }

    int ret = 0;
    struct timeval timeout;

    for (int attempt = 1; attempt <= DSM_DSCV_MAX_RETRIES; attempt++) {
        LOG_INFO("\nTentative [%d/%d] — Demande de connexion en cours...\n", attempt,
                 DSM_DSCV_MAX_RETRIES);

        ssize_t sent_bytes = sendto(send_sock, msg_buf, msg_len, 0,
                                    (struct sockaddr *)&addr, sizeof(addr));
        if (sent_bytes < 0) {
            LOG_ERROR("sendto");
            ret = -1;
            goto exit;
        }

        // Attention 1 reset/tentative
        timeout.tv_sec = DSM_DSCV_RESEND_INTERVAL_SEC;
        timeout.tv_usec = 0;
    }

exit:
    close(send_sock);
    return ret;
}

static int discover_scan_network() {

    return 0;
}

int dsm_discover() {

    if (discover_sockets_init() < 0) {
        // TODO tcp_port_connect_spvs + seria + deseria
        return -1;
    }

    if (discover_scan_network() < 0) {
        return -1;
    }

    return 0;
}