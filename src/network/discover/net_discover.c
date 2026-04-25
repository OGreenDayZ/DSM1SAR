#include "net_discover.h"

#include "debug/debug.h"
#include "internal.h"
#include "network/discover/candidate.h"
#include "network/discover/supervisor.h"
#include "network/message/serializer.h"
#include "network/network.h"
#include <sys/time.h>

static dsm_discover_ctx *dscv_ctx;

static int dscv_ctx_init() {
    LOG_INFO("Initialisation du contexte de découverte ...");

    if (dscv_ctx) {
        LOG_ERROR("dscv_ctx déjà initialisé ?!");
        return -1;
    }

    dscv_ctx = malloc(sizeof(dsm_discover_ctx));
    if (!dscv_ctx) {
        LOG_ERROR("malloc : dscv_ctx");
        return -1;
    }

    dscv_ctx->sock_tcp_listen = -1;

    if (dsm_get_local_ip(&dscv_ctx->my_ip) < 0) {
        return -1;
    }

    unsigned short *listen_udp_port = &dsm_ctx->config.net.node.udp_port;
    dscv_ctx->sock_udp_recv = dsm_udp_bind_unicast(*listen_udp_port);
    if (dscv_ctx->sock_udp_recv < 0) {
        goto exit_udp_error;
    }

    if (*listen_udp_port == 0) {
        struct sockaddr_in local_addr;
        socklen_t local_addr_len = sizeof(local_addr);
        if (getsockname(dscv_ctx->sock_udp_recv, (struct sockaddr *)&local_addr,
                        &local_addr_len) < 0) {
            LOG_ERROR("getsockname");
            goto exit_udp_error;
        }
        *listen_udp_port = ntohs(local_addr.sin_port);
    }

    if (inet_pton(AF_INET, dsm_ctx->config.net.system.multicast_ip,
                  &dscv_ctx->multicast_ip) <= 0) {
        LOG_ERROR("inet_pton pour l'IP multicast a échoué : %s",
                  dsm_ctx->config.net.system.multicast_ip);
        goto exit_udp_error;
    }
    dscv_ctx->multicast_ip = ntohl(dscv_ctx->multicast_ip);
    dscv_ctx->sock_udp_mcast = dsm_udp_bind_multicast(
        dscv_ctx->multicast_ip, dsm_ctx->config.net.system.multicast_port);

    if (dscv_ctx->sock_udp_mcast < 0) {
        goto exit_mcast_error;
    }

    return 0;

exit_mcast_error:
    close(dscv_ctx->sock_udp_recv);
    dscv_ctx->sock_udp_recv = -1;

exit_udp_error:
    close(dscv_ctx->sock_udp_mcast);
    dscv_ctx->sock_udp_mcast = -1;

    return -1;
}

/**
 * @note : Ceux là ne devraient plus être ouverts à ce stade, mais on les ferme au cas où
 * + debug
 * @note : si des fonctions précédentes sortent par -1, peut faire effet
 */
static void dscv_ctx_cleanup() {
    if (dscv_ctx->sock_tcp_listen >= 0) {
        LOG_INFO("Fermeture du socket TCP d'écoute ...");
        close(dscv_ctx->sock_tcp_listen);
        dscv_ctx->sock_tcp_listen = -1;
    }

    if (dscv_ctx->sock_udp_recv >= 0) {
        LOG_INFO("Fermeture du socket UDP de réception ...");
        close(dscv_ctx->sock_udp_recv);
        dscv_ctx->sock_udp_recv = -1;
    }

    if (dscv_ctx->sock_udp_mcast >= 0) {
        LOG_INFO("Fermeture du socket UDP multicast ...");
        close(dscv_ctx->sock_udp_mcast);
        dscv_ctx->sock_udp_mcast = -1;
    }

    free(dscv_ctx);
    dscv_ctx = NULL;
}

/**
 *@return : -1 erreur, 0 trouvé, 1 non trouvé
 */
static int recv_dscv_spvs_data(uint32_t *spvs_ip, uint16_t *spvs_tcp_port) {
    LOG_INFO("En attente d'un CODE=%d ...", CODE_DISCOVER_SUPERVISOR_DATA);

    struct timeval start, current, timeout;
    gettimeofday(&start, NULL);

    size_t buf_size = sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_spvs_data);
    uint8_t buf[buf_size];
    uint32_t src_ip;
    uint16_t src_port;

    while (1) {
        gettimeofday(&current, NULL);
        long elapsed_us = (current.tv_sec - start.tv_sec) * 1000000L +
                          (current.tv_usec - start.tv_usec);
        long remaining_us = (DSM_DSCV_RESEND_INTERVAL_SEC * 1000000L) - elapsed_us;

        if (remaining_us <= 0) {
            return 1;
        }

        timeout.tv_sec = remaining_us / 1000000L;
        timeout.tv_usec = remaining_us % 1000000L;

        int ret = dsm_udp_recvfrom(dscv_ctx->sock_udp_recv, buf, buf_size, &src_ip,
                                   &src_port, &timeout);
        if (ret < 0) {
            return 1;
        } else if ((size_t)ret != buf_size) {
            LOG_WARNING(
                "Message du superviseur reçu mais de taille inattendue : %d octets", ret);
            continue;
        }

        dsm_msg_header header;
        int offset = dsm_dsrlze_header(buf, buf_size, &header);
        if (offset < 0) {
            continue;
        }

        if (header.id != dsm_ctx->config.net.system.system_id ||
            header.code != CODE_DISCOVER_SUPERVISOR_DATA) {
            LOG_WARNING("Message CODE ignoré...");
            continue;
        }

        LOG_INFO("CODE=%d reçu du superviseur", CODE_DISCOVER_SUPERVISOR_DATA);

        dsm_msg_dscv_spvs_data spvs_data;
        offset = dsm_dsrlze_dscv_spvs_data(buf + offset, buf_size - offset, &spvs_data);
        if (offset < 0) {
            continue;
        }

        LOG_INFO("Superviseur trouvé à l'adresse Multicast, ses données sont %u:%u",
                 src_ip, src_port);

        if (spvs_ip) {
            *spvs_ip = src_ip;
        }
        if (spvs_tcp_port) {
            *spvs_tcp_port = spvs_data.tcp_port_spvs;
        }

        return 0;
    }
}

/**
 * @return : -1 erreur, 0 noeud normal, 1 superviseur
 */
static int send_dscv_init(uint32_t *spvs_ip, uint16_t *spvs_tcp_port) {
    LOG_INFO("Recherche d'un Superviseur ...");

    struct sockaddr_in addr;
    dsm_make_addr(dscv_ctx->multicast_ip, dsm_ctx->config.net.system.multicast_port,
                  &addr);

    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_init)];
    int msg_len = dsm_srlze_dscv_init(msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int send_sock = dsm_udp_bind_unicast(0);
    if (send_sock < 0) {
        return -1;
    }

    int ret = 0;
    struct sockaddr_in local_addr;
    socklen_t local_addr_len = sizeof(local_addr);
    if (getsockname(send_sock, (struct sockaddr *)&local_addr, &local_addr_len) < 0) {
        LOG_ERROR("getsockname");
        ret = -1;
        goto exit;
    }
    dscv_ctx->udp_send_port = ntohs(local_addr.sin_port);

    for (int attempt = 1; attempt <= DSM_DSCV_MAX_RETRIES; attempt++) {
        LOG_INFO("\n ----- Tentative [%d/%d] — Demande de connexion en cours... -----",
                 attempt, DSM_DSCV_MAX_RETRIES);
        LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_INIT);
        LOG_INFO("Envoi de %zu octets", msg_len);
        ssize_t sent = sendto(send_sock, msg_buf, msg_len, 0, (struct sockaddr *)&addr,
                              sizeof(addr));
        if (sent < 0) {
            ret = -1;
            goto exit;
        }

        ret = recv_dscv_spvs_data(spvs_ip, spvs_tcp_port);
        if (ret < 0) {
            goto exit;
        } else if (ret == 0) { // Superviseur trouvé
            break;
        } else {
            LOG_INFO("Aucun superviseur trouvé lors de cette tentative");
        }
    }
    // assert ret == 1 => aucun superviseur trouvé après toutes les tentatives

exit:
    close(send_sock);
    return ret;
}

int dsm_discover() {
    int ret = 0;

    if (dscv_ctx_init() < 0) {
        ret = -1;
        goto exit;
    }

    uint32_t spvs_ip;
    uint16_t spvs_tcp_port;
    int is_supervisor = send_dscv_init(&spvs_ip, &spvs_tcp_port);
    close(dscv_ctx->sock_udp_recv);
    dscv_ctx->sock_udp_recv = -1;
    if (is_supervisor < 0) {
        LOG_ERROR("Erreur lors de la découverte du superviseur");
        ret = -1;
        goto exit;
    }

    if (is_supervisor) {
        LOG_INFO("Aucun superviseur trouvé après [%d] tentatives !",
                 DSM_DSCV_MAX_RETRIES);
        LOG_INFO("Elu en tant que superviseur du système");
        dsm_ctx->node_id = 0;

        if (dsm_supervising_start(dscv_ctx) < 0) {
            ret = -1;
            goto exit;
        }

    } else {

        close(dscv_ctx->sock_udp_mcast);
        dscv_ctx->sock_udp_mcast = -1;

        if (dsm_candidate_start(dscv_ctx, spvs_ip, spvs_tcp_port) < 0) {
            ret = -1;
            goto exit;
        }
    }
exit:
    dscv_ctx_cleanup();
    return ret;
}