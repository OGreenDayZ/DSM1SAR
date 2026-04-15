#include "net_discover.h"

#include <stdlib.h>
#include <unistd.h>

#include "debug/debug.h"
#include "internal.h"
#include "network/discover/supervisor.h"
#include "network/message/serializer.h"
#include "network/network.h"
#include "util/hashmap.h"

static dsm_dscv_ctx discover_ctx;

static int discover_ctx_init() {
    LOG_INFO("Initialisation du contexte de découverte ...");

    discover_ctx.sock_tcp_listen = -1;
    discover_ctx.supervisor_tcp_sock = -1;

    if (dsm_get_local_ip(&discover_ctx.my_ip) < 0) {
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

/* @return : -1 erreur, 0 trouvé, 1 non trouvé */
static int recv_dscv_spvs_data() {
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
            LOG_INFO("Aucun superviseur trouvé lors de cette tentative");
            return 1;
        }

        timeout.tv_sec = remaining_us / 1000000L;
        timeout.tv_usec = remaining_us % 1000000L;

        if (dsm_udp_recvfrom(discover_ctx.sock_udp_recv, buf, buf_size, &src_ip,
                             &src_port, &timeout) < 0) {
            LOG_INFO("Aucun superviseur trouvé lors de cette tentative");
            return 1;
        }

        dsm_msg_header header;
        int offset = dsm_dsrlze_header(buf, buf_size, &header);
        if (offset < 0) {
            continue;
        }

        if (header.id != dsm_ctx->config.net.system.system_id ||
            header.code != CODE_DISCOVER_SUPERVISOR_DATA) {
            LOG_WARNING("Message ignoré...");
            continue;
        }

        dsm_msg_dscv_spvs_data spvs_data;
        offset = dsm_dsrlze_dscv_spvs_data(buf + offset, buf_size - offset, &spvs_data);
        if (offset < 0) {
            continue;
        }

        LOG_INFO("Superviseur trouvé à %s:%d; port TCP à connecter: %d", src_ip, src_port,
                 spvs_data.tcp_port_spvs);

        discover_ctx.supervisor_ip = src_ip;
        discover_ctx.supervisor_tcp_port = spvs_data.tcp_port_spvs;

        return 0;
    }
}

/* @return : -1 erreur, 0 noeud normal, 1 superviseur */
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
        LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_INIT);
        ssize_t sent_bytes = sendto(send_sock, msg_buf, msg_len, 0,
                                    (struct sockaddr *)&addr, sizeof(addr));
        if (sent_bytes < 0) {
            ret = -1;
            goto exit;
        }

        ret = recv_dscv_spvs_data();
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
@return : -1 erreur, 0 succès, 1 système complet
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

/* TODO FACTO AVEC connect_to_managers ? */
static int prepare_nodes_reg(const dsm_msg_dscv_share_reg *share) {

    for (uint32_t i = 0; i < share->nb_nodes; i++) {

        const dsm_reg_node *reg_node = &share->nodes[i];

        dsm_node_content *node_content = malloc(sizeof(dsm_node_content));
        if (!node_content) {
            LOG_ERROR("Erreur malloc : node_content");
            return -1;
        }

        node_content->id = reg_node->id;
        node_content->ip = reg_node->ip;

        if (reg_node->id == 0) {
            discover_ctx.supervisor_role = DSM_ROLE_WORKER;
            node_content->tcp_sock = discover_ctx.supervisor_tcp_sock;
        } else {
            node_content->tcp_sock = -1;
        }

        dsm_hash_add(dsm_ctx->nodes_reg, &node_content->node, node_content->id);
    }
    return 0;
}

static int connect_to_managers(const dsm_msg_dscv_share_reg *share) {

    for (uint32_t i = 0; i < share->nb_nodes; i++) {

        const dsm_reg_node *reg_node = &share->nodes[i];

        dsm_node_content *node_content = malloc(sizeof(dsm_node_content));
        if (!node_content) {
            LOG_ERROR("Erreur malloc : node_content");
            return -1;
        }

        node_content->id = reg_node->id;
        node_content->ip = reg_node->ip;

        if (reg_node->id != 0) {
            discover_ctx.supervisor_role = DSM_ROLE_MANAGER;
            node_content->tcp_sock = discover_ctx.supervisor_tcp_sock;

        } else {
            int manager_sock = dsm_tcp_connect_sock(reg_node->ip, reg_node->manager_port);
            if (manager_sock < 0) {
                LOG_ERROR("Connexion au manager %u échouée", reg_node->id);
                return -1;
            }
            node_content->tcp_sock = manager_sock;
        }

        dsm_hash_add(dsm_ctx->nodes_reg, &node_content->node, node_content->id);
    }
    return 0;
}

static int recv_dscv_share_reg() {
    LOG_INFO("En attente d'un CODE=%d || CODE=%d ...",
             CODE_DISCOVER_SHARE_REGISTRY_MANAGER, CODE_DISCOVER_SHARE_REGISTRY_WORKER);
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
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK);

    discover_ctx.sock_tcp_listen = dsm_tcp_bind_listen(dsm_ctx->config.net.node.tcp_port);
    if (discover_ctx.sock_tcp_listen < 0) {
        LOG_ERROR(
            "Erreur lors de la liaison du socket TCP pour les connexions entrantes");
        return -1;
    }

    uint8_t buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_share_reg_ack)];
    int msg_len =
        dsm_srlze_dscv_share_reg_ack(dsm_ctx->config.net.node.tcp_port, buf, sizeof(buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent_bytes = dsm_tcp_send_all(discover_ctx.supervisor_tcp_sock, buf, msg_len);
    if (sent_bytes < 0) {
        return -1;
    }

    return 0;
}

static int wait_workers_connect() {
    LOG_INFO("En attente de connexions des workers ...");
    int connected_workers = discover_ctx.supervisor_role == DSM_ROLE_WORKER ? 1 : 0;
    int expected_workers = DSM_HASH_COUNT(dsm_ctx->nodes_reg);

    while (connected_workers != expected_workers) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(discover_ctx.sock_tcp_listen,
                                 (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) {
            LOG_ERROR("accept");
            return -1;
        }
        // Comparer ID et IP
        uint32_t client_ip = ntohl(client_addr.sin_addr.s_addr);
        dsm_node_content *node_content;
        /* @note : dsm_hash_for_each_possible pour la sécurité,
         * même si ID est normalement unique */
        dsm_hash_for_each_possible(dsm_ctx->nodes_reg, node_content, node, client_ip) {
            if (node_content->ip == client_ip) {
                node_content->tcp_sock = client_sock;
                connected_workers++;
                LOG_INFO("Worker %u connecté", node_content->id);
                goto continue_accept;
            }
        }
        close(client_sock);
        LOG_ERROR("Connexion d'un client inconnu : IP %u", client_ip);
        return -1;

    continue_accept:;
    }

    return 0;
}

static int send_dscv_mgr_ready() {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_MANAGER_READY);

    uint8_t buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_dscv_mgr_ready(buf, sizeof(buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent_bytes = dsm_tcp_send_all(discover_ctx.supervisor_tcp_sock, buf, msg_len);
    if (sent_bytes < 0) {
        return -1;
    }

    return 0;
}

static int recv_dscv_wkr_config() {
    LOG_INFO("En attente d'un CODE=%d ...", CODE_DISCOVER_WORKER_CONFIG);
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
    if (header.code != CODE_DISCOVER_WORKER_CONFIG) {
        LOG_ERROR("Code inconnu : %d", header.code);
        return -1;
    }

    buf_size = sizeof(header.payload_len);
    uint8_t pbuf[buf_size];
    timeout.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    if (dsm_tcp_recv_all(discover_ctx.supervisor_tcp_sock, pbuf, buf_size, &timeout) <
        0) {
        return -1;
    }
    dsm_msg_dscv_wkr_config config;
    if (dsm_dsrlze_dscv_wkr_config(pbuf, buf_size, &config) < 0) {
        return -1;
    }

    dsm_ctx->config.pager.nb_pages = config.nb_pages;
    dsm_ctx->config.pager.page_size = config.page_size;

    LOG_INFO("Configuration de la mémoire partagée reçue du superviseur : %u pages de "
             "%u octets chacune",
             config.nb_pages, config.page_size);
    return 0;
}

static int send_dscv_wkr_config_ack() {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_WORKER_CONFIG_ACK);

    uint8_t buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_dscv_wkr_config_ack(buf, sizeof(buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent_bytes = dsm_tcp_send_all(discover_ctx.supervisor_tcp_sock, buf, msg_len);
    if (sent_bytes < 0) {
        return -1;
    }

    return 0;
}

static int recv_dscv_finalize() {
    LOG_INFO("En attente d'un CODE=%d ...", CODE_DISCOVER_FINALIZE);
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
    if (header.code != CODE_DISCOVER_FINALIZE) {
        LOG_ERROR("Code inconnu : %d", header.code);
        return -1;
    }

    LOG_INFO("Finalisation de la phase de découverte par le superviseur");
    return 0;
}

// TODO FACTO
int dsm_discover() {

    if (discover_ctx_init() < 0) {
        return -1;
    }

    int is_supervisor = send_dscv_init();
    close(discover_ctx.sock_udp_recv);
    discover_ctx.sock_udp_recv = -1;
    if (is_supervisor < 0) {
        LOG_ERROR("Erreur lors de la découverte du superviseur");
        return -1;
    }

    if (is_supervisor) {
        LOG_INFO("Elu en tant que superviseur du système");
        dsm_ctx->node_id = 0;

        if (dsm_supervisor_loop(&discover_ctx) < 0) {
            return -1;
        }

    } else {

        close(discover_ctx.sock_udp_mcast);
        discover_ctx.sock_udp_mcast = -1;

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

            if (wait_workers_connect() < 0) {
                return -1;
            }
            close(discover_ctx.sock_tcp_listen);
            discover_ctx.sock_tcp_listen = -1;

            if (send_dscv_mgr_ready() < 0) {
                return -1;
            }

        } else if (dsm_ctx->config.net.node.role == DSM_ROLE_WORKER) {
            if (recv_dscv_wkr_config() < 0) {
                return -1;
            }
            if (send_dscv_wkr_config_ack() < 0) {
                return -1;
            }

        } else {
            LOG_ERROR("Rôle non attribué ?");
            return -1;
        }

        if (recv_dscv_finalize() < 0) {
            return -1;
        }

        if (discover_ctx.supervisor_role == dsm_ctx->config.net.node.role) {
            close(discover_ctx.supervisor_tcp_sock);
            discover_ctx.supervisor_tcp_sock = -1;
        }
    }

    return 0;
}