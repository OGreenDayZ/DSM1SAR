#include "supervisor.h"

#include <poll.h>
#include <string.h>

#include "debug/debug.h"
#include "internal.h"
#include "network/discover/spvs_factory.h"
#include "network/manager/manager.h"
#include "network/message/serializer.h"
#include "network/network.h"
#include "util/concurrent/atomicity.h"

static dsm_supervisor_ctx *spvs_ctx;

static int spvs_ctx_init(dsm_discover_ctx *dscv_ctx) {
    LOG_INFO("Initialisation du contexte du superviseur ...");

    if (spvs_ctx) {
        LOG_ERROR("spvs_ctx déjà initialisé ?!");
        return -1;
    }

    unsigned short *listen_port = &dsm_ctx->config.net.node.tcp_port;
    dscv_ctx->sock_tcp_listen = dsm_tcp_bind_listen(*listen_port);
    if (dscv_ctx->sock_tcp_listen < 0) {
        return -1;
    }

    if (*listen_port == 0) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        if (getsockname(dscv_ctx->sock_tcp_listen, (struct sockaddr *)&addr, &addr_len) <
            0) {
            LOG_ERROR("getsockname");
            close(dscv_ctx->sock_tcp_listen);
            return -1;
        }
        *listen_port = ntohs(addr.sin_port);
    }

    spvs_ctx = malloc(sizeof(dsm_supervisor_ctx));
    if (!spvs_ctx) {
        LOG_ERROR("malloc : spvs_ctx");
        return -1;
    }

    atomic_store(&spvs_ctx->state, DSM_SPVS_STATE_SCANNING);
    atomic_store(&spvs_ctx->id_count, 0);

    if (dsm_safe_list_init(&spvs_ctx->eligible_ip) < 0) {
        return -1;
    }

    if (dsm_safe_list_init(&spvs_ctx->poll_fds_update) < 0) {
        return -1;
    }

    spvs_ctx->pool = pool_create(dsm_ctx->config.net.node.pool_nb_threads,
                                 dsm_ctx->config.net.node.pool_queue_size);

    atomic_store(&spvs_ctx->join_count, 0);
    atomic_store(&spvs_ctx->role_wish_count, 0);
    atomic_store(&spvs_ctx->share_mgr_count, 0);
    atomic_store(&spvs_ctx->mgr_ready_count, 0);
    atomic_store(&spvs_ctx->wkr_config_count, 0);

    if (dsm_safe_map_init(&spvs_ctx->spvs_reg, DSM_SPVS_HASH_BITS) < 0) {
        return -1;
    }

    return 0;
}

static void spvs_ctx_cleanup(dsm_discover_ctx *dscv_ctx) {

    close(dscv_ctx->sock_tcp_listen);
    dscv_ctx->sock_tcp_listen = -1;

    dsm_eligible_entry *pos_e, *tmp_pos_e;
    dsm_safe_list_for_each_entry_safe(&spvs_ctx->eligible_ip, pos_e, tmp_pos_e, node) {
        dsm_list_del(&pos_e->node);
        free(pos_e);
    }
    dsm_safe_list_destroy(&spvs_ctx->eligible_ip);

    dsm_poll_update_entry *pos_pu, *tmp_pos_pu;
    dsm_safe_list_for_each_entry_safe(&spvs_ctx->poll_fds_update, pos_pu, tmp_pos_pu,
                                      node) {
        dsm_list_del(&pos_pu->node);
        free(pos_pu);
    }
    dsm_safe_list_destroy(&spvs_ctx->poll_fds_update);

    pool_destroy(spvs_ctx->pool, graceful_shutdown);

    dsm_node_role my_role = dsm_ctx->config.net.node.role;

    unsigned int bkt;
    struct dsm_hlist_node *tmp;
    dsm_supervising_node *pos;

    dsm_hash_safe_for_each_safe(&spvs_ctx->spvs_reg, bkt, tmp, pos, node) {

        if (my_role == pos->role && pos->tcp_sock >= 0) {
            close(pos->tcp_sock);
        } else {
            dsm_node_content *node_content = malloc(sizeof(dsm_node_content));
            if (node_content) {
                node_content->id = pos->id;
                node_content->tcp_sock = pos->tcp_sock;
                dsm_init_hlist_node(&node_content->node);
                dsm_hash_safe_add(&dsm_ctx->nodes_reg, &node_content->node,
                                  node_content->id);

                if (my_role == DSM_ROLE_WORKER) {
                    // Liaison
                    dsm_mgr_to_node_content *shortcut = pos->set_shortcut_mgr;
                    shortcut->to_node_content = node_content;
                }
            } else {
                LOG_ERROR("malloc : node_content");
            }
        }

        dsm_hlist_del_init(&pos->node);
        free(pos);
    }

    dsm_safe_map_destroy(&spvs_ctx->spvs_reg);

    free(spvs_ctx);
    spvs_ctx = NULL;
}

static void get_current_role_stats(size_t *managers_count, size_t *workers_count) {
    size_t m_count = 0;
    size_t w_count = 0;

    if (dsm_ctx->config.net.node.role == DSM_ROLE_MANAGER) {
        m_count++;
    } else if (dsm_ctx->config.net.node.role == DSM_ROLE_WORKER) {
        w_count++;
    }

    unsigned int bkt;
    dsm_supervising_node *pos;
    dsm_hash_safe_for_each(&spvs_ctx->spvs_reg, bkt, pos, node) {
        if (pos->role == DSM_ROLE_MANAGER) {
            m_count++;
        } else if (pos->role == DSM_ROLE_WORKER) {
            w_count++;
        }
    }

    *managers_count = m_count;
    *workers_count = w_count;
}

static void role_check(dsm_node_role *role, size_t *nb_assign_m, size_t *nb_assign_w,
                       size_t *nb_convert_m, size_t *nb_convert_w) {

    // LOG_INFO("nb_assign_m=%zu, nb_assign_w=%zu, nb_convert_m=%zu, nb_convert_w=%zu",
    //          *nb_assign_m, *nb_assign_w, *nb_convert_m, *nb_convert_w);
    switch (*role) {
        case DSM_ROLE_UNDEFINED:
            if (*nb_assign_m > 0) {
                *role = DSM_ROLE_MANAGER;
                (*nb_assign_m)--;
            } else if (nb_assign_w > 0) {
                *role = DSM_ROLE_WORKER;
                (*nb_assign_w)--;
            }
            break;

        case DSM_ROLE_MANAGER:
            if (*nb_convert_w > 0) {
                *role = DSM_ROLE_WORKER;
                (*nb_convert_w)--;
            }
            break;

        case DSM_ROLE_WORKER:
            if (*nb_convert_m > 0) {
                *role = DSM_ROLE_MANAGER;
                (*nb_convert_m)--;
            }
            break;
    }
}

static int role_assignement() {
    LOG_INFO("Début : Attribution des rôles aux nodes...");

    size_t m_count, w_count, nb_undifined, expected_m, expected_w;
    get_current_role_stats(&m_count, &w_count);
    nb_undifined = dsm_ctx->config.net.system.nb_nodes - m_count - w_count;

    if (m_count + w_count + nb_undifined != dsm_ctx->config.net.system.nb_nodes) {
        LOG_ERROR("Incohérence dans le comptage des rôles");
        return -1;
    }

    expected_m = dsm_ctx->config.net.system.nb_managers;
    expected_w = dsm_ctx->config.net.system.nb_nodes - expected_m;

    LOG_INFO(
        "Rôles actuels (avant assignement) : %zu managers, %zu workers, %zu undefined",
        m_count, w_count, nb_undifined);
    /**
     * @note : nb_assign_X -> nb_assign_TO_m
     * @note : nb_convert_X -> nb_convert_TO_m
     */
    size_t nb_assign_m = 0, nb_assign_w = 0, nb_convert_m = 0, nb_convert_w = 0;
    if (m_count < expected_m) {
        size_t needed_m = expected_m - m_count;
        nb_assign_m = nb_undifined >= needed_m ? needed_m : nb_undifined;
        nb_undifined -= nb_assign_m;
        needed_m -= nb_assign_m;
        nb_convert_m = needed_m;
    }

    if (w_count < expected_w) {
        size_t needed_w = expected_w - w_count;
        nb_assign_w = nb_undifined >= needed_w ? needed_w : nb_undifined;
        nb_undifined -= nb_assign_w;
        needed_w -= nb_assign_w;
        nb_convert_w = needed_w;
    }

    dsm_node_role *my_role = &dsm_ctx->config.net.node.role;
    role_check(my_role, &nb_assign_m, &nb_assign_w, &nb_convert_m, &nb_convert_w);
    LOG_INFO("Rôle du superviseur : %s", *my_role == DSM_ROLE_MANAGER  ? "MANAGER"
                                         : *my_role == DSM_ROLE_WORKER ? "WORKER"
                                                                       : "UNDEFINED");

    if (*my_role == DSM_ROLE_MANAGER) {
        if (dsm_mgr_ctx_init() < 0) {
            return -1;
        }
    } else if (*my_role == DSM_ROLE_WORKER) {
        if (dsm_wkr_ctx_init() < 0) {
            return -1;
        }
    }

    unsigned int bkt;
    dsm_supervising_node *pos;
    dsm_hash_safe_for_each(&spvs_ctx->spvs_reg, bkt, pos, node) {
        role_check(&pos->role, &nb_assign_m, &nb_assign_w, &nb_convert_m, &nb_convert_w);
        LOG_INFO("Node ID %u : rôle %s", pos->id,
                 pos->role == DSM_ROLE_MANAGER
                     ? "MANAGER"
                     : (pos->role == DSM_ROLE_WORKER ? "WORKER" : "UNDEFINED"));
    }

    LOG_INFO("Fin : Attribution des rôles aux nodes");
    return 0;
}

/**
 * @note : Fonction thread start routine
 */
static void dispatch_call_udp(void *arg) {
    dsm_msg_udp_thread_ctx *msg_ctx = (dsm_msg_udp_thread_ctx *)arg;

    switch (msg_ctx->header.code) {
        case CODE_DISCOVER_INIT:
            if (atomic_load(&spvs_ctx->state) != DSM_SPVS_STATE_SCANNING ||
                treat_dscv_init(msg_ctx, spvs_ctx) < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_DISCOVER_INIT);
            }
            break;

        default:
            LOG_WARNING("Code de message UDP inconnu ou non traitable: %d",
                        msg_ctx->header.code);
    }

    free(msg_ctx->payload);
    free(msg_ctx);
    return;
}

/**
 * @note : Fonction thread start routine
 */
static void dispatch_call_tcp(void *arg) {
    dsm_msg_tcp_thread_ctx *msg_ctx = (dsm_msg_tcp_thread_ctx *)arg;

    switch (msg_ctx->header.code) {
        case CODE_DISCOVER_ROLE_WISH:
            if (atomic_load(&spvs_ctx->state) != DSM_SPVS_STATE_SCANNING ||
                treat_dscv_role_wish(msg_ctx, spvs_ctx) < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_DISCOVER_ROLE_WISH);
            }
            break;

        case CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK:
            if (atomic_load(&spvs_ctx->state) != DSM_SPVS_STATE_MGR_SHARING ||
                treat_dscv_share_reg_ack(msg_ctx, spvs_ctx) < 0) {
                LOG_WARNING("Message non traité : CODE=%d",
                            CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK);
            }
            break;

        case CODE_DISCOVER_MANAGER_READY:
            if (/**
                 * atomic_load compare désactivé à cause du race
                 */
                // atomic_load(&spvs_ctx->state) != DSM_SPVS_STATE_MGR_WAITING ||
                treat_dscv_mgr_ready(spvs_ctx) < 0) {
                LOG_WARNING("Message non traité : CODE=%d", CODE_DISCOVER_MANAGER_READY);
            }
            break;

        case CODE_DISCOVER_WORKER_CONFIG_ACK:
            if (atomic_load(&spvs_ctx->state) != DSM_SPVS_STATE_WKR_CONFIGURING ||
                treat_dscv_wkr_config_ack(spvs_ctx) < 0) {
                LOG_WARNING("Message non traité : CODE=%d",
                            CODE_DISCOVER_WORKER_CONFIG_ACK);
            }
            break;

        default:
            LOG_WARNING("Code de message TCP inconnu ou non traitable: %d",
                        msg_ctx->header.code);
    }

    free(msg_ctx->payload);
    free(msg_ctx);
    return;
}

static int handle_udp_mcast(dsm_discover_ctx *dscv_ctx) {
    LOG_INFO("Message reçu sur la socket UDP multicast");

    int ret = 0;
    dsm_msg_udp_thread_ctx *msg_ctx = malloc(sizeof(dsm_msg_udp_thread_ctx));
    if (!msg_ctx) {
        LOG_ERROR("malloc : msg_ctx");
        return -1;
    }
    struct timeval tv = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};

    uint8_t buf[DSM_MTU_SIZE];

    if (dsm_udp_recvfrom(dscv_ctx->sock_udp_mcast, buf, sizeof(buf),
                         &msg_ctx->src_addr.sin_addr.s_addr, &msg_ctx->src_addr.sin_port,
                         &tv) < 0) {
        ret = -1;
        goto exit_err_ctx;
    }

    int offset = dsm_dsrlze_header(buf, sizeof(buf), &msg_ctx->header);
    if (offset < 0 || offset + msg_ctx->header.payload_len > sizeof(buf)) {
        LOG_ERROR("Erreur de désérialisation du header ou payload trop grand");
        ret = -1;
        goto exit_err_ctx;
    }

    msg_ctx->payload = malloc(msg_ctx->header.payload_len);
    if (!msg_ctx->payload) {
        LOG_ERROR("malloc : msg_ctx->payload");
        ret = -1;
        goto exit_err_ctx;
    }

    if (msg_ctx->src_addr.sin_addr.s_addr == dscv_ctx->my_ip &&
        msg_ctx->src_addr.sin_port == dscv_ctx->udp_send_port) {
        LOG_INFO("Message multicast reçu de moi-même, ignoré");
        goto exit_err_payload;
    }

    if (msg_ctx->header.id != dsm_ctx->config.net.system.system_id) {
        LOG_INFO("Message multicast reçu d'un autre système, ignoré");
        goto exit_err_payload;
    }

    memcpy(msg_ctx->payload, buf + offset, msg_ctx->header.payload_len);

    if (pool_add_task(spvs_ctx->pool, dispatch_call_udp, msg_ctx) < 0) {
        ret = -1;
        goto exit_err_payload;
    }

    return 0;

exit_err_payload:
    free(msg_ctx->payload);

exit_err_ctx:
    free(msg_ctx);

    return ret;
}

/**
 * @note : Fonction thread start routine
 */
static void accept_entry(void *arg) {
    dsm_eligible_ctx eli_ctx = *(dsm_eligible_ctx *)arg;
    free(arg);

    dsm_eligible_entry *pos, *n;
    dsm_safe_list_for_each_entry_safe(&spvs_ctx->eligible_ip, pos, n, node) {
        if (pos->ip == eli_ctx.client_ip) {
            dsm_list_del(&pos->node);
            free(pos);
            dsm_safe_list_unlock(&spvs_ctx->eligible_ip);
            LOG_INFO("Client éligible connecté");
            goto found;
        }
    }
    LOG_ERROR("Connexion d'un client non éligible");
    goto exit_err_socket;

    dsm_supervising_node *node_content;
    dsm_poll_update_entry *update_entry;
    unsigned int id_node;
found:

    bool added = increment_if_less_ext(&spvs_ctx->id_count,
                                       dsm_ctx->config.net.system.nb_nodes - 1, &id_node);

    if (!added) {
        LOG_WARNING("Refus d'un client : nombre maximum de nodes déjà atteint");
        if (send_dscv_sys_full(eli_ctx.sock) < 0) {
            // LOG_ERROR("Erreur envoi message CODE_DISCOVER_SYS_FULL");
        }
        goto exit_err_socket;
    }

    node_content = malloc(sizeof(dsm_supervising_node));
    if (!node_content) {
        LOG_ERROR("malloc : node_content");
        goto exit_err_socket;
    }

    node_content->id = id_node;
    node_content->tcp_sock = eli_ctx.sock;
    node_content->ip = eli_ctx.client_ip;
    node_content->manager_port =
        0; // sera set avec CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK
    node_content->role = DSM_ROLE_UNDEFINED; // si existe, set par CODE_DISCOVER_ROLE_WISH
    dsm_init_hlist_node(&node_content->node);
    dsm_hash_safe_add(&spvs_ctx->spvs_reg, &node_content->node, id_node);

    update_entry = malloc(sizeof(dsm_poll_update_entry));
    if (!update_entry) {
        LOG_ERROR("Erreur malloc : update_entry");
        goto exit_err_node;
    }

    update_entry->id_node = id_node;
    update_entry->fd = eli_ctx.sock;
    dsm_safe_list_add(&spvs_ctx->poll_fds_update, &update_entry->node);

    if (send_dscv_join_ack(eli_ctx.sock, id_node) < 0) {
        goto exit_err_node;
    }

    atomic_fetch_add(&spvs_ctx->join_count, 1);

    LOG_INFO("Acceptation d'un client, attribution du ID_Node=%u", id_node);

    return;

// exit_err_entry:
//     free(update_entry);
exit_err_node:
    free(node_content);
exit_err_socket:
    close(eli_ctx.sock);

    return;
}

static int handle_tcp_listen(dsm_discover_ctx *dscv_ctx) {
    LOG_INFO("Message reçu sur la socket TCP d'écoute");

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_sock =
        accept(dscv_ctx->sock_tcp_listen, (struct sockaddr *)&client_addr, &addr_len);

    if (client_sock < 0) {
        LOG_ERROR("accept : %d", client_sock);
        return -1;
    }

    dsm_eligible_ctx *eli_ctx = malloc(sizeof(dsm_eligible_ctx));
    if (!eli_ctx) {
        LOG_ERROR("malloc : eli_ctx");
        close(client_sock);
        return -1;
    }

    eli_ctx->sock = client_sock;
    eli_ctx->client_ip = ntohl(client_addr.sin_addr.s_addr);
    if (pool_add_task(spvs_ctx->pool, accept_entry, eli_ctx) < 0) {
        free(eli_ctx);
        close(client_sock);
        return -1;
    }

    return 0;
}

static int handle_tcp_recv(int poll_index) {
    unsigned int id_node = poll_index - DSM_SPVS_POLL_FDS_SERVER + 1;
    LOG_INFO("Message reçu sur une socket TCP (ID_Node=%d)", id_node);

    dsm_msg_tcp_thread_ctx *msg_ctx = malloc(sizeof(dsm_msg_tcp_thread_ctx));
    if (!msg_ctx) {
        LOG_ERROR("malloc : msg_ctx");
        return -1;
    }

    int recv_sock = -1;
    dsm_supervising_node *node_content;
    dsm_hash_safe_get(&spvs_ctx->spvs_reg, node_content, node, id_node) {
        if (node_content->id == id_node) {
            recv_sock = node_content->tcp_sock;
            // dsm_hash_safe_unlock(&spvs_ctx->spvs_reg, id_node);
            // break;
        }
    }
    dsm_hash_safe_get_end(&spvs_ctx->spvs_reg, id_node);

    if (recv_sock < 0) {
        LOG_ERROR("Socket de réception introuvable pour le node ID %u", id_node);
        goto exit_err_ctx;
    }

    uint8_t hbuf[sizeof(dsm_msg_header)];
    struct timeval tv = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};
    int ret_recv = dsm_tcp_recv_all(recv_sock, hbuf, sizeof(hbuf), &tv);
    if (ret_recv < 0) {
        if (ret_recv == -2) {
            goto exit_updt_close;
        }
        goto exit_err_ctx;
    }

    if (dsm_dsrlze_header(hbuf, sizeof(hbuf), &msg_ctx->header) < 0) {
        goto exit_err_ctx;
    }

    // malloc 0 ? TODO
    msg_ctx->payload = malloc(msg_ctx->header.payload_len);
    if (!msg_ctx->payload) {
        LOG_ERROR("malloc : msg_ctx->payload");
        goto exit_err_ctx;
    }

    tv.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC;
    tv.tv_usec = 0;
    ret_recv =
        dsm_tcp_recv_all(recv_sock, msg_ctx->payload, msg_ctx->header.payload_len, &tv);
    if (ret_recv < 0) {
        if (ret_recv == -2) {
            goto exit_updt_close;
        }
        goto exit_err_payload;
    }

    msg_ctx->sock = recv_sock; // pour les réponses direct si besoin

    if (pool_add_task(spvs_ctx->pool, dispatch_call_tcp, msg_ctx) < 0) {
        goto exit_err_payload;
    }

    return 0;

    dsm_poll_update_entry *update_entry;
exit_updt_close:
    // TODO tolérance exit ?
    close(recv_sock);
    update_entry = malloc(sizeof(dsm_poll_update_entry));
    if (!update_entry) {
        LOG_ERROR("Erreur malloc : update_entry");
        goto exit_err_payload;
    }
    update_entry->id_node = id_node;
    update_entry->fd = -1;
    dsm_safe_list_add(&spvs_ctx->poll_fds_update, &update_entry->node);

exit_err_payload:
    free(msg_ctx->payload);

exit_err_ctx:
    free(msg_ctx);

    return -1;
}

static int update_spvs_state(dsm_discover_ctx *dscv_ctx) {
    unsigned int expected;
    bool is_manager = dsm_ctx->config.net.node.role == DSM_ROLE_MANAGER;

    // LOG_INFO("State=%d, join_count=%u, role_wish_count=%u, share_mgr_count=%u, "
    //          "mgr_ready_count=%u, wkr_config_count=%u",
    //          atomic_load(&spvs_ctx->state), atomic_load(&spvs_ctx->join_count),
    //          atomic_load(&spvs_ctx->role_wish_count),
    //          atomic_load(&spvs_ctx->share_mgr_count),
    //          atomic_load(&spvs_ctx->mgr_ready_count),
    //          atomic_load(&spvs_ctx->wkr_config_count));
    switch (atomic_load(&spvs_ctx->state)) {

        case DSM_SPVS_STATE_SCANNING:
            expected = dsm_ctx->config.net.system.nb_nodes - 1;
            if (atomic_load(&spvs_ctx->join_count) == expected &&
                atomic_load(&spvs_ctx->role_wish_count) == expected) {
                atomic_store(&spvs_ctx->state, DSM_SPVS_STATE_MGR_SHARING);
                if (role_assignement() < 0 ||
                    send_dscv_share_reg(spvs_ctx, dscv_ctx,
                                        CODE_DISCOVER_SHARE_REGISTRY_MANAGER) < 0) {
                    return -1;
                }
            }
            return 0;

        case DSM_SPVS_STATE_MGR_SHARING:
            expected = dsm_ctx->config.net.system.nb_managers - (is_manager ? 1 : 0);
            if (atomic_load(&spvs_ctx->share_mgr_count) == expected) {
                atomic_store(&spvs_ctx->state, DSM_SPVS_STATE_MGR_WAITING);
                if (send_dscv_share_reg(spvs_ctx, dscv_ctx,
                                        CODE_DISCOVER_SHARE_REGISTRY_WORKER) < 0) {
                    return -1;
                }
            }
            return 0;

        case DSM_SPVS_STATE_MGR_WAITING:
            expected = dsm_ctx->config.net.system.nb_managers - (is_manager ? 1 : 0);
            if (atomic_load(&spvs_ctx->mgr_ready_count) == expected) {
                atomic_store(&spvs_ctx->state, DSM_SPVS_STATE_WKR_CONFIGURING);
                if (send_dscv_wkr_config(spvs_ctx) < 0) {
                    return -1;
                }
            }
            return 0;

        case DSM_SPVS_STATE_WKR_CONFIGURING:
            expected = dsm_ctx->config.net.system.nb_nodes -
                       dsm_ctx->config.net.system.nb_managers - (is_manager ? 0 : 1);

            // print debug
            LOG_INFO("Attendu %u workers configurés, actuellement %u", expected,
                     atomic_load(&spvs_ctx->wkr_config_count));
            if (atomic_load(&spvs_ctx->wkr_config_count) == expected) {
                atomic_store(&spvs_ctx->state, DSM_SPVS_STATE_FINALIZING);
                // -> send finalize dans fin de boucle de dsm_supervising_start
            }
            return 0;

        default:
            return 0;
    }
}

static void update_poll_fds(struct pollfd *fds, int max_fds, int skip_fds) {
    int index;
    dsm_poll_update_entry *pos, *n;
    dsm_safe_list_for_each_entry_safe(&spvs_ctx->poll_fds_update, pos, n, node) {
        index =
            pos->id_node + skip_fds - 1; // -1 car ID_Node=0 est pris par le superviseur
        if (index >= 0 && index < max_fds) {
            fds[index].fd = pos->fd;
            LOG_INFO("Socket de Node ID:%u mis à jour à %d", pos->id_node, pos->fd);
        }
        dsm_list_del(&pos->node);
        free(pos);
    }
}

int dsm_supervising_start(dsm_discover_ctx *dscv_ctx) {
    int ret = 0;

    int max_fds = DSM_SPVS_POLL_FDS_SERVER + dsm_ctx->config.net.system.nb_nodes - 1;
    struct pollfd fds[max_fds];

    if (spvs_ctx_init(dscv_ctx) < 0) {
        ret = -1;
        goto exit;
    }

    for (int i = 0; i < max_fds; i++) {
        fds[i].fd = -1;
        fds[i].events = POLLIN;
    }

    fds[0].fd = dscv_ctx->sock_udp_mcast;
    fds[1].fd = dscv_ctx->sock_tcp_listen;
    while (atomic_load(&spvs_ctx->state) != DSM_SPVS_STATE_FINALIZING) {

        if (update_spvs_state(dscv_ctx) < 0) {
            ret = -1;
            goto exit;
        }
        update_poll_fds(fds, max_fds, DSM_SPVS_POLL_FDS_SERVER);

        int ret = poll(fds, max_fds, DSM_SPVS_POLL_TIMEOUT_MS);
        if (ret < 0) {
            LOG_ERROR("poll");
            ret = -1;
            goto exit;

        } else if (ret == 0) { /* timeout */
            continue;
        }

        for (int i = 0; i < max_fds; i++) {
            if (fds[i].revents & POLLIN) {

                if (i == 0) {
                    if (handle_udp_mcast(dscv_ctx) < 0) {
                        continue;
                    }
                } else if (i == 1) {
                    if (handle_tcp_listen(dscv_ctx) < 0) {
                        continue;
                    }
                } else {
                    if (handle_tcp_recv(i) < 0) {
                        continue;
                    }
                }
            }
        }
    }

    if (send_dscv_finalize(spvs_ctx) < 0) {
        ret = -1;
        goto exit;
    }

    LOG_INFO("Découverte terminée avec succès");
exit:
    spvs_ctx_cleanup(dscv_ctx);
    return ret;
}