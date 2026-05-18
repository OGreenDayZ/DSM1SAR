#include "candidate.h"

#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "debug/debug.h"
#include "dsm.h"
#include "internal.h"
#include "network/discover/net_discover.h"
#include "network/manager/manager.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "network/network.h"
#include "network/worker/worker.h"
#include "util/concurrent/safe_map.h"
#include "util/hlist.h"

static dsm_candidate_ctx *cand_ctx;

static int cand_ctx_init(uint32_t spvs_ip, uint16_t spvs_tcp_port) {
    LOG_INFO("Initialisation du contexte du candidat ...");

    if (cand_ctx) {
        LOG_ERROR("cand_ctx déjà initialisé ?!");
        return -1;
    }

    cand_ctx = malloc(sizeof(dsm_candidate_ctx));
    if (!cand_ctx) {
        LOG_ERROR("malloc : cand_ctx");
        return -1;
    }

    cand_ctx->supervisor_ip = spvs_ip;
    cand_ctx->supervisor_tcp_port = spvs_tcp_port;
    cand_ctx->supervisor_tcp_sock = -1;
    cand_ctx->supervisor_role = DSM_ROLE_UNDEFINED;

    if (dsm_safe_map_init(&cand_ctx->cand_reg, DSM_CAND_HASH_BITS) < 0) {
        return -1;
    }
    return 0;
}

static void cand_ctx_cleanup() {

    if (cand_ctx->supervisor_role == dsm_ctx->config.net.node.role) {
        close(cand_ctx->supervisor_tcp_sock);
    }
    cand_ctx->supervisor_tcp_sock = -1;

    dsm_cand_content *pos;
    unsigned int bkt;
    struct dsm_hlist_node *tmp;
    dsm_node_role my_role = dsm_ctx->config.net.node.role;

    dsm_hash_safe_for_each_safe(&cand_ctx->cand_reg, bkt, tmp, pos, node) {
        dsm_node_content *node_content = malloc(sizeof(dsm_node_content));
        if (node_content) {
            if (pthread_mutex_init(&node_content->send_lock, NULL) < 0) {
                LOG_ERROR("pthread_mutex_init : node_content->send_lock");
                free(node_content);
                dsm_hash_safe_unlock(&cand_ctx->cand_reg, bkt);
                return;
            }
            node_content->id = pos->id;
            node_content->tcp_sock = pos->tcp_sock;
            dsm_init_hlist_node(&node_content->node);
            dsm_hash_safe_add(&dsm_ctx->nodes_reg, &node_content->node, node_content->id);

            if (my_role == DSM_ROLE_WORKER) {
                // Liaison
                dsm_mgr_to_node_content *shortcut = pos->set_shortcut_mgr;
                shortcut->to_node_content = node_content;
            }

        } else {
            LOG_ERROR("malloc : node_content");
        }
        dsm_hlist_del_init(&pos->node);
        free(pos);
    }

    dsm_safe_map_destroy(&cand_ctx->cand_reg);

    free(cand_ctx);
    cand_ctx = NULL;
}

static int connect_to_supervisor() {
    cand_ctx->supervisor_tcp_sock =
        dsm_tcp_connect_sock(cand_ctx->supervisor_ip, cand_ctx->supervisor_tcp_port);

    if (cand_ctx->supervisor_tcp_sock < 0) {
        LOG_ERROR("Connexion au superviseur échouée");
        return -1;
    }

    LOG_INFO("Connecté au superviseur à l'adresse %u:%u", cand_ctx->supervisor_ip,
             cand_ctx->supervisor_tcp_port);
    return 0;
}

/**
 * @return : -1 erreur, 0 succès, 1 système complet
 */
static int recv_dscv_join_ack() {
    LOG_INFO("En attente d'un CODE=%d ...", CODE_DISCOVER_JOIN_ACK);

    struct timeval timeout = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};
    size_t buf_size = sizeof(dsm_msg_header);
    uint8_t hbuf[buf_size];

    if (dsm_tcp_recv_all(cand_ctx->supervisor_tcp_sock, hbuf, buf_size, &timeout) < 0) {
        return -1;
    }

    dsm_msg_header header;
    if (dsm_dsrlze_header(hbuf, buf_size, &header) < 0) {
        return -1;
    }

    if (header.code == CODE_DISCOVER_JOIN_ACK) {
        LOG_INFO("CODE=%d reçu du superviseur", CODE_DISCOVER_JOIN_ACK);
        buf_size = header.payload_len;
        uint8_t pbuf[buf_size];
        timeout.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC;
        timeout.tv_usec = 0;
        if (dsm_tcp_recv_all(cand_ctx->supervisor_tcp_sock, pbuf, buf_size, &timeout) <
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

static int send_dscv_role_wish() {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_ROLE_WISH);

    uint8_t buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_role_wish)];
    int msg_len = dsm_srlze_dscv_role_wish(buf, sizeof(buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(cand_ctx->supervisor_tcp_sock, buf, msg_len);
    if (sent < 0) {
        return -1;
    }

    return 0;
}

static int prepare_nodes_reg(const dsm_msg_dscv_share_reg *share) {

    for (uint32_t i = 0; i < share->nb_nodes; i++) {

        const dsm_reg_node *reg_node = &share->nodes[i];

        dsm_cand_content *cand_content = malloc(sizeof(dsm_cand_content));
        if (!cand_content) {
            LOG_ERROR("Erreur malloc : cand_content");
            return -1;
        }

        cand_content->id = reg_node->id;
        cand_content->ip = reg_node->ip;
        LOG_INFO("Préparation du registre de noeuds : ID=%u, IP=%u", cand_content->id,
                 cand_content->ip);
        if (reg_node->id == 0) {
            cand_ctx->supervisor_role = DSM_ROLE_WORKER;
            cand_content->tcp_sock = cand_ctx->supervisor_tcp_sock;
        } else {
            cand_content->tcp_sock = -1;
        }

        dsm_init_hlist_node(&cand_content->node);
        dsm_hash_safe_add(&cand_ctx->cand_reg, &cand_content->node, cand_content->id);
    }
    return 0;
}

static int send_dscv_wkr_id(int manager_sock) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_WORKER_IDENTITY);

    uint8_t buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_dscv_wkr_id(buf, sizeof(buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(manager_sock, buf, msg_len);
    if (sent < 0) {
        return -1;
    }

    return 0;
}

static int recv_dscv_wkr_id_ack(int manager_sock) {
    LOG_INFO("En attente d'un CODE=%d ...", CODE_DISCOVER_WORKER_IDENTITY_ACK);

    struct timeval timeout = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};
    size_t buf_size = sizeof(dsm_msg_header);
    uint8_t hbuf[buf_size];

    if (dsm_tcp_recv_all(manager_sock, hbuf, buf_size, &timeout) < 0) {
        return -1;
    }

    dsm_msg_header header;
    if (dsm_dsrlze_header(hbuf, buf_size, &header) < 0) {
        return -1;
    }

    if (header.code == CODE_DISCOVER_WORKER_IDENTITY_ACK) {
        LOG_INFO("CODE=%d reçu du manager", CODE_DISCOVER_WORKER_IDENTITY_ACK);
        return 0;

    } else {
        LOG_ERROR("Code inconnu : %d", header.code);
        return -1;
    }
}

static int connect_to_managers(const dsm_msg_dscv_share_reg *share) {

    LOG_INFO("Connexion aux managers avec le registre ...");
    for (uint32_t i = 0; i < share->nb_nodes; i++) {
        const dsm_reg_node *reg_node = &share->nodes[i];

        dsm_cand_content *node_content = malloc(sizeof(dsm_cand_content));
        if (!node_content) {
            LOG_ERROR("Erreur malloc : node_content");
            return -1;
        }

        dsm_mgr_to_node_content *convert_node_content =
            malloc(sizeof(dsm_mgr_to_node_content));
        if (!convert_node_content) {
            LOG_ERROR("Erreur malloc : convert_node_content");
            free(node_content);
            return -1;
        }

        node_content->id = reg_node->id;
        node_content->ip = reg_node->ip;
        node_content->set_shortcut_mgr = convert_node_content;

        convert_node_content->manager_id = i;
        convert_node_content->node_id = reg_node->id;

        if (reg_node->id == 0) {
            cand_ctx->supervisor_role = DSM_ROLE_MANAGER;
            node_content->tcp_sock = cand_ctx->supervisor_tcp_sock;

        } else {
            LOG_INFO("Connexion au manager ID %u ...", reg_node->id);
            int manager_sock = dsm_tcp_connect_sock(reg_node->ip, reg_node->manager_port);
            if (manager_sock < 0) {
                LOG_ERROR("Connexion au manager %u échouée", reg_node->id);
                return -1;
            }

            LOG_INFO("Connecté au manager ID %u", reg_node->id);

            if (send_dscv_wkr_id(manager_sock) < 0) {
                LOG_ERROR("Envoi de l'identité du worker au manager %u échoué",
                          reg_node->id);
                close(manager_sock);
                return -1;
            }

            if (recv_dscv_wkr_id_ack(manager_sock) < 0) {
                LOG_ERROR("Vous n'avez pas été reconnu par le manager");
                close(manager_sock);
                return -1;
            }

            node_content->tcp_sock = manager_sock;
            LOG_INFO("Vous avez été reconnu par le manager ID %u", reg_node->id);
        }

        dsm_init_hlist_node(&node_content->node);
        dsm_hash_safe_add(&cand_ctx->cand_reg, &node_content->node, node_content->id);

        dsm_init_hlist_node(&convert_node_content->node);
        dsm_hash_safe_add(&dsm_wkr_ctx->managerID_to_nodeID, &convert_node_content->node,
                          convert_node_content->manager_id);
    }
    return 0;
}

static int recv_dscv_share_reg() {
    LOG_INFO("En attente d'un CODE=%d || CODE=%d ...",
             CODE_DISCOVER_SHARE_REGISTRY_MANAGER, CODE_DISCOVER_SHARE_REGISTRY_WORKER);

    size_t buf_size = sizeof(dsm_msg_header);
    uint8_t hbuf[buf_size];

    // Pas de timeout ici, le processus est plus long
    if (dsm_tcp_recv_all(cand_ctx->supervisor_tcp_sock, hbuf, buf_size, NULL) < 0) {
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

        buf_size = header.payload_len;
        uint8_t pbuf[buf_size];
        struct timeval timeout = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};
        if (dsm_tcp_recv_all(cand_ctx->supervisor_tcp_sock, pbuf, buf_size, &timeout) <
            0) {
            return -1;
        }

        dsm_msg_dscv_share_reg *share;
        if (dsm_dsrlze_dscv_share_reg(pbuf, buf_size, &share) < 0) {
            return -1;
        }

        dsm_ctx->config.net.system.nb_nodes = share->sys_total_nodes;

        LOG_INFO(
            "Registre de noeuds reçu du superviseur : %u noeuds dans un système de %u "
            "noeuds",
            share->nb_nodes, share->sys_total_nodes);

        if (header.code == CODE_DISCOVER_SHARE_REGISTRY_MANAGER) {
            LOG_INFO("Rôle assigné : MANAGER");
            dsm_ctx->config.net.system.nb_managers =
                share->sys_total_nodes - share->nb_nodes;
            if (dsm_mgr_ctx_init() || prepare_nodes_reg(share) < 0) {
                free(share);
                return -1;
            }

        } else { // CODE_DISCOVER_SHARE_REGISTRY_WORKER
            LOG_INFO("Rôle assigné : WORKER");
            dsm_ctx->config.net.system.nb_managers = share->nb_nodes;
            if (dsm_wkr_ctx_init() || connect_to_managers(share) < 0) {
                free(share);
                return -1;
            }
        }
        free(share);
    }
    return 0;
}

static int send_dscv_share_reg_ack(dsm_discover_ctx *dscv_ctx) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK);

    unsigned short *listen_port = &dsm_ctx->config.net.node.tcp_port;
    dscv_ctx->sock_tcp_listen = dsm_tcp_bind_listen(*listen_port);

    if (*listen_port == 0) {
        struct sockaddr_in local_addr;
        socklen_t local_addr_len = sizeof(local_addr);
        if (getsockname(dscv_ctx->sock_tcp_listen, (struct sockaddr *)&local_addr,
                        &local_addr_len) < 0) {
            LOG_ERROR("getsockname");
            return -1;
        }
        *listen_port = ntohs(local_addr.sin_port);
    }

    if (dscv_ctx->sock_tcp_listen < 0) {
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

    int sent = dsm_tcp_send_all(cand_ctx->supervisor_tcp_sock, buf, msg_len);
    if (sent < 0) {
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK);
    return 0;
}

static int recv_dscv_wkr_id(int worker_sock, uint32_t *worker_id) {
    LOG_INFO("En attente d'un CODE=%d ...", CODE_DISCOVER_WORKER_IDENTITY);

    struct timeval timeout = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};
    size_t buf_size = sizeof(dsm_msg_header);
    uint8_t hbuf[buf_size];

    if (dsm_tcp_recv_all(worker_sock, hbuf, buf_size, &timeout) < 0) {
        return -1;
    }

    dsm_msg_header header;
    if (dsm_dsrlze_header(hbuf, buf_size, &header) < 0) {
        return -1;
    }

    if (header.code == CODE_DISCOVER_WORKER_IDENTITY) {
        LOG_INFO("CODE=%d reçu du worker", CODE_DISCOVER_WORKER_IDENTITY);
        *worker_id = header.id;
        return 0;

    } else {
        LOG_ERROR("Code inconnu : %d", header.code);
        return -1;
    }
}

static int send_dscv_wkr_id_ack(int worker_sock) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_WORKER_IDENTITY_ACK);

    uint8_t buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_dscv_wkr_id_ack(buf, sizeof(buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(worker_sock, buf, msg_len);
    if (sent < 0) {
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_DISCOVER_WORKER_IDENTITY_ACK);
    return 0;
}

static int wait_workers_connect(dsm_discover_ctx *dscv_ctx) {
    LOG_INFO("En attente de connexions des workers ...");

    int connected_workers = cand_ctx->supervisor_role == DSM_ROLE_WORKER ? 1 : 0;
    int expected_workers = dsm_safe_map_count(&cand_ctx->cand_reg);
    bool found;

    while (connected_workers != expected_workers) {
        found = false;
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock =
            accept(dscv_ctx->sock_tcp_listen, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) {
            LOG_ERROR("accept");
            return -1;
        }
        LOG_INFO("Nouvelle connexion, vérification de l'identité du worker...");

        // Comparer ID et IP
        uint32_t client_id;
        if (recv_dscv_wkr_id(client_sock, &client_id) < 0) {
            close(client_sock);
            return -1;
        }

        uint32_t client_ip = ntohl(client_addr.sin_addr.s_addr);

        dsm_cand_content *node_content;
        /**
         * @note : dsm_hash_for_each_possible pour la sécurité,
         * même si ID est normalement unique
         */
        dsm_hash_safe_get(&cand_ctx->cand_reg, node_content, node, client_id) {
            if (node_content->id == client_id && node_content->ip == client_ip) {
                node_content->tcp_sock = client_sock;
                connected_workers++;
                LOG_INFO("Worker %u connecté", node_content->id);
                found = true;
                // dsm_hash_safe_unlock(&cand_ctx->cand_reg, client_id);
                // break; // On sort du dsm_hash_safe_get, pas du while !!
            }
        }
        dsm_hash_safe_get_end(&cand_ctx->cand_reg, client_id);

        if (send_dscv_wkr_id_ack(client_sock) < 0) {
            close(client_sock);
            return -1;
        }

        if (!found) {
            close(client_sock);
            LOG_ERROR("Connexion refusée : client inconnu IP=%u", client_ip);
        }
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

    int sent = dsm_tcp_send_all(cand_ctx->supervisor_tcp_sock, buf, msg_len);
    if (sent < 0) {
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_DISCOVER_MANAGER_READY);
    return 0;
}

static int recv_dscv_wkr_config() {
    LOG_INFO("En attente d'un CODE=%d ...", CODE_DISCOVER_WORKER_CONFIG);

    struct timeval timeout = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};
    size_t buf_size = sizeof(dsm_msg_header);
    uint8_t hbuf[buf_size];

    if (dsm_tcp_recv_all(cand_ctx->supervisor_tcp_sock, hbuf, buf_size, &timeout) < 0) {
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

    buf_size = header.payload_len;
    uint8_t pbuf[buf_size];
    timeout.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    if (dsm_tcp_recv_all(cand_ctx->supervisor_tcp_sock, pbuf, buf_size, &timeout) < 0) {
        return -1;
    }

    LOG_INFO("CODE=%d reçu du superviseur", CODE_DISCOVER_WORKER_CONFIG);
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

    int sent = dsm_tcp_send_all(cand_ctx->supervisor_tcp_sock, buf, msg_len);
    if (sent < 0) {
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_DISCOVER_WORKER_CONFIG_ACK);
    return 0;
}

static int recv_dscv_finalize() {
    LOG_INFO("En attente d'un CODE=%d ...", CODE_DISCOVER_FINALIZE);

    struct timeval timeout = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};
    size_t buf_size = sizeof(dsm_msg_header);
    uint8_t hbuf[buf_size];

    if (dsm_tcp_recv_all(cand_ctx->supervisor_tcp_sock, hbuf, buf_size, &timeout) < 0) {
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
    LOG_INFO("CODE=%d reçu du superviseur", CODE_DISCOVER_FINALIZE);

    LOG_INFO("Finalisation de la phase de découverte par le superviseur");
    return 0;
}

int dsm_candidate_start(dsm_discover_ctx *dscv_ctx, uint32_t spvs_ip,
                        uint16_t spvs_tcp_port) {

    int ret = 0;

    if (cand_ctx_init(spvs_ip, spvs_tcp_port) < 0) {
        ret = -1;
        goto exit;
    }

    if (connect_to_supervisor() < 0) {
        ret = -1;
        goto exit;
    }

    ret = recv_dscv_join_ack();
    if (ret < 0) {
        ret = -1;
        goto exit;
    } else if (ret == 1) {
        // TODO propagation
        return -DSM_ERROR_SYSTEM_FULL;
    }

    if (send_dscv_role_wish() < 0) {
        ret = -1;
        goto exit;
    }

    if (recv_dscv_share_reg() < 0) {
        ret = -1;
        goto exit;
    }

    if (dsm_ctx->config.net.node.role == DSM_ROLE_MANAGER) {
        if (send_dscv_share_reg_ack(dscv_ctx) < 0) {
            ret = -1;
            goto exit;
        }

        if (wait_workers_connect(dscv_ctx) < 0) {
            ret = -1;
            goto exit;
        }
        close(dscv_ctx->sock_tcp_listen);
        dscv_ctx->sock_tcp_listen = -1;

        if (send_dscv_mgr_ready() < 0) {
            ret = -1;
            goto exit;
        }

    } else if (dsm_ctx->config.net.node.role == DSM_ROLE_WORKER) {
        if (recv_dscv_wkr_config() < 0) {
            ret = -1;
            goto exit;
        }
        if (send_dscv_wkr_config_ack() < 0) {
            ret = -1;
            goto exit;
        }

    } else {
        LOG_ERROR("Rôle non attribué ?");
        ret = -1;
        goto exit;
    }

    if (recv_dscv_finalize() < 0) {
        ret = -1;
        goto exit;
    }

exit:
    cand_ctx_cleanup();
    return ret;
}