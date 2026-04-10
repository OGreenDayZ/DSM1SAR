#include "net_discover.h"

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug/debug.h"
#include "dsm.h"
#include "internal.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "network/network.h"
#include "util/hashmap.h"

static dsm_dscv_ctx discover_ctx;

static int discover_sockets_init() {
    LOG_INFO("Initialisation des sockets UDP de découverte ...");
    int ret = dsm_get_local_ip(&discover_ctx.my_ip);
    if (ret < 0) {
        return -1;
    }

    discover_ctx.sock_udp_recv = dsm_udp_bind_unicast(dsm_ctx->config.net.node.udp_port);
    if (discover_ctx.sock_udp_recv < 0) {
        goto exit_udp_error;
    }

    if (inet_pton(AF_INET, dsm_ctx->config.net.system.multicast_ip,
                  &discover_ctx.multicast_ip) <= 0) {
        LOG_ERROR("inet_pton pour l'IP multicast a échoué : %s",
                  dsm_ctx->config.net.system.multicast_ip);
        goto exit_udp_error;
    }
    discover_ctx.sock_udp_mcast = dsm_udp_bind_multicast(
        discover_ctx.multicast_ip, dsm_ctx->config.net.system.multicast_port);

    if (discover_ctx.sock_udp_mcast < 0) {
        goto exit_mcast_error;
    }

    return 0;

exit_mcast_error:
    close(discover_ctx.sock_udp_recv);

exit_udp_error:
    close(discover_ctx.sock_udp_mcast);
    return -1;
}

static int recv_dscv_spvs_data() {
    LOG_INFO("En attente d'un CODE=%d ...", CODE_DISCOVER_SUPERVISOR_DATA);
    struct timeval timeout = {.tv_sec = DSM_DSCV_RESEND_INTERVAL_SEC, .tv_usec = 0};
    size_t buf_size = sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_spvs_data);
    uint8_t buf[buf_size];
    uint32_t src_ip;
    uint16_t src_port;

    if (dsm_udp_recvfrom(discover_ctx.sock_udp_recv, buf, buf_size, &src_ip, &src_port,
                         &timeout) < 0) {
        LOG_INFO("Aucun superviseur trouvé lors de cette tentative");
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

    discover_ctx.supervisor_ip = src_ip;
    discover_ctx.supervisor_tcp_port = spvs_data.tcp_port_spvs;

    return 0;
}

static int send_dscv_init() {
    LOG_INFO("Recherche d'un Superviseur ...");
    struct sockaddr_in addr;
    dsm_make_addr(discover_ctx.multicast_ip, dsm_ctx->config.net.system.multicast_port,
                  &addr);

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

    LOG_INFO("Connecté au superviseur %s:%d", discover_ctx.supervisor_ip,
             discover_ctx.supervisor_tcp_port);
    return 0;
}

/*
@note : Gérer retour -> 0 = succès, 1 = système complet, -1 = erreur
*/
static int recv_dscv_join_ack() {
    LOG_INFO("En attente d'un CODE=%d ...", CODE_DISCOVER_JOIN_ACK);
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

    if (header.code == CODE_DISCOVER_JOIN_ACK) {
        buf_size = sizeof(header.payload_len);
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
        return 1;

    } else {
        LOG_ERROR("Code inconnu : %d", header.code);
        return -1;
    }
}

static int prepare_nodes_reg(const dsm_msg_dscv_share_reg *share) {

    for (uint32_t i = 0; i < share->nb_nodes; i++) {
        const dsm_reg_node *reg_node = &share->nodes[i];
        dsm_node_content node_content = {
            .id = reg_node->id,
            .tcp_sock = -1,
            .ip = reg_node->ip,
        };
        dsm_hash_add(dsm_ctx->nodes_reg, &node_content.node, node_content.id);
    }
    return 0;
}

static int connect_to_managers(const dsm_msg_dscv_share_reg *share) {
    for (uint32_t i = 0; i < share->nb_nodes; i++) {
        int manager_sock =
            dsm_tcp_connect_sock(share->nodes[i].ip, share->nodes[i].manager_port);
        if (manager_sock < 0) {
            LOG_ERROR("Connexion au manager %u échouée", share->nodes[i].id);
            return -1;
        }

        const dsm_reg_node *reg_node = &share->nodes[i];
        dsm_node_content node_content = {
            .id = reg_node->id,
            .tcp_sock = manager_sock,
            .ip = reg_node->ip,
        };
        dsm_hash_add(dsm_ctx->nodes_reg, &node_content.node, node_content.id);
    }
    return 0;
}

static int recv_dscv_share_reg() {
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

    if (header.code == CODE_DISCOVER_SHARE_REGISTRY_MANAGER ||
        header.code == CODE_DISCOVER_SHARE_REGISTRY_WORKER) {
        dsm_ctx->config.net.node.role =
            (header.code == CODE_DISCOVER_SHARE_REGISTRY_MANAGER) ? DSM_ROLE_MANAGER
                                                                  : DSM_ROLE_WORKER;

        buf_size = sizeof(header.payload_len);
        uint8_t pbuf[buf_size];
        timeout.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC;
        timeout.tv_usec = 0;
        if (dsm_tcp_recv_all(discover_ctx.supervisor_tcp_sock, pbuf, buf_size, &timeout) <
            0) {
            return -1;
        }

        dsm_msg_dscv_share_reg share;
        if (dsm_dsrlze_dscv_share_reg(pbuf, buf_size, &share) < 0) {
            return -1;
        }

        dsm_ctx->config.net.system.nb_nodes = share.sys_total_nodes;
        // TODO ADD HASHTABLE

        LOG_INFO("Registre de nœuds reçu du superviseur : %u nœuds dans un système de %u "
                 "nœuds",
                 share.nb_nodes, share.sys_total_nodes);

        if (header.code == CODE_DISCOVER_SHARE_REGISTRY_MANAGER) {
            LOG_INFO("Rôle assigné : MANAGER");
            dsm_ctx->config.net.system.nb_managers = share.nb_nodes;
            if (prepare_nodes_reg(&share) < 0) {
                return -1;
            }

        } else { // CODE_DISCOVER_SHARE_REGISTRY_WORKER
            LOG_INFO("Rôle assigné : WORKER");
            dsm_ctx->config.net.system.nb_managers =
                share.sys_total_nodes - share.nb_nodes;
            if (connect_to_managers(&share) < 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int send_dscv_share_reg_ack() {
    LOG_INFO("Envoi d'un ACK de partage de registre au superviseur ...");
    uint8_t buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_share_reg_ack)];
    int msg_len =
        dsm_srlze_dscv_share_reg_ack(dsm_ctx->config.net.node.tcp_port, buf, sizeof(buf));
    if (msg_len < 0) {
        return -1;
    }

    ssize_t sent_bytes = send(discover_ctx.supervisor_tcp_sock, buf, msg_len, 0);
    if (sent_bytes < 0) {
        LOG_ERROR("send");
        return -1;
    }

    return 0;
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

        int ret = recv_dscv_join_ack();
        if (ret < 0) {
            return -1;
        } else if (ret == 1) {
            return DSM_ERROR_SYSTEM_FULL;
        }

        if (recv_dscv_share_reg() < 0) {
            return -1;
        }

        if (dsm_ctx->config.net.node.role == DSM_ROLE_MANAGER) {
            if (send_dscv_share_reg_ack() < 0) {
                return -1;
            }

        } else if (dsm_ctx->config.net.node.role == DSM_ROLE_WORKER) {

        } else {
            LOG_ERROR("Rôle non attribué ?");
            return -1;
        }

    } else {
        LOG_INFO("Elu en tant que superviseur du système");
        dsm_ctx->node_id = 0;
    }

    return 0;
}