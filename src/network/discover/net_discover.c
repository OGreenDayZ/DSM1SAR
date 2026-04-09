#include "net_discover.h"

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
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

static int recv_dscv_spvs_data() {
    struct timeval timeout = {.tv_sec = DSM_DSCV_RESEND_INTERVAL_SEC, .tv_usec = 0};
    size_t buf_size = sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_spvs_data);
    uint8_t buf[buf_size];
    char src_ip[INET_ADDRSTRLEN];
    int src_port;

    if (dsm_udp_recvfrom(discover_ctx.sock_mcast_udp, buf, buf_size, src_ip, &src_port,
                         &timeout) < 0) {
        return -1;
    }

    dsm_msg_header header;
    int offset = dsm_dsrlze_header(buf, buf_size, &header);
    if (offset < 0) {
        return -1;
    }
    if (header.code != CODE_DISCOVER_SUPERVISOR_DATA) {
        LOG_ERROR("CODE reçu incorrect : %d", header.code, src_ip, src_port);
        return -1;
    }

    dsm_msg_dscv_spvs_data spvs_data;
    offset = dsm_dsrlze_dscv_spvs_data(buf + offset, buf_size - offset, &spvs_data);
    if (offset < 0) {
        return -1;
    }

    LOG_INFO("Superviseur trouvé à %s:%d; port TCP à connecter: %d", src_ip, src_port,
             spvs_data.tcp_port_spvs);

    memcpy(discover_ctx.supervisor_ip, src_ip, INET_ADDRSTRLEN);
    discover_ctx.supervisor_tcp_port = spvs_data.tcp_port_spvs;

    return 0;
}

static int send_dscv_init() {
    LOG_INFO("Recherche d'un Superviseur ...");
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

    for (int attempt = 1; attempt <= DSM_DSCV_MAX_RETRIES; attempt++) {
        LOG_INFO("\nTentative [%d/%d] — Demande de connexion en cours...", attempt,
                 DSM_DSCV_MAX_RETRIES);

        ssize_t sent_bytes = sendto(send_sock, msg_buf, msg_len, 0,
                                    (struct sockaddr *)&addr, sizeof(addr));
        if (sent_bytes < 0) {
            LOG_ERROR("sendto");
            ret = -1;
            goto exit;
        }

        if (recv_dscv_spvs_data() < 0) {
            continue;
        } else {
            goto exit;
        }
    }

    discover_ctx.is_supervisor = true;

exit:
    close(send_sock);
    return ret;
}

static int connect_to_supervisor() {
    discover_ctx.supervisor_tcp_sock = dsm_tcp_connect_sock(
        discover_ctx.supervisor_ip, discover_ctx.supervisor_tcp_port);

    if (discover_ctx.supervisor_tcp_sock < 0) {
        LOG_ERROR("Connexion au superviseur échouée");
        return -1;
    }
    return 0;
}

static int recv_dscv_join_ack() {
    struct timeval timeout = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};
    size_t buf_size = sizeof(dsm_msg_header);
    uint8_t hbuf[buf_size];

    if (dsm_tcp_recv_all(discover_ctx.supervisor_tcp_sock, hbuf, buf_size, &timeout) <
        0) {
        return -1;
    }

    dsm_msg_header header;
    if (dsm_dsrlze_header(hbuf, buf_size, &header) < 0) {
        return -1;
    }

    if (header.code != CODE_DISCOVER_JOIN_ACK) {
        buf_size = sizeof(dsm_msg_dscv_join_ack);
        uint8_t pbuf[buf_size];
        timeout.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC;
        timeout.tv_usec = 0;
        if (dsm_tcp_recv_all(discover_ctx.supervisor_tcp_sock, pbuf, buf_size, &timeout) <
            0) {
            return -1;
        }
        dsm_msg_dscv_join_ack payload;
        if (dsm_dsrlze_dscv_join_ack(pbuf, buf_size, &payload) < 0) {
            return -1;
        }
        dsm_ctx->node_id = payload.assigned_id;
        LOG_INFO("Le superviseur a accepté l'entrée avec l'ID assigné: %u",
                 payload.assigned_id);
        return 0;

    } else if (header.code == CODE_DISCOVER_SYSTEM_FULL) {
        LOG_ERROR("Le superviseur a rejeté l'entrée : système complet");
        return -1;

    } else {
        LOG_ERROR("Code inconnu : %d", header.code);
        return -1;
    }
}

int dsm_discover() {

    if (discover_sockets_init() < 0) {
        return -1;
    }

    if (send_dscv_init() < 0) {
        LOG_ERROR("Erreur lors de la découverte du superviseur");
        return -1;
    }

    if (!discover_ctx.is_supervisor) {
        if (connect_to_supervisor() < 0) {
            return -1;
        }

        LOG_INFO("Connecté au superviseur %s:%d", discover_ctx.supervisor_ip,
                 discover_ctx.supervisor_tcp_port);

        if (recv_dscv_join_ack() < 0) {
            return -1;
        }

    } else {
        LOG_INFO("Elu en tant que superviseur du système");
        dsm_ctx->node_id = 0;
    }

    return 0;
}