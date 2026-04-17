#include "supervisor.h"

#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug/debug.h"
#include "internal.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "network/network.h"
#include "util/atomicity.h"

static dsm_supervisor_ctx *spvs_ctx;

static int supervisor_config(dsm_dscv_ctx *dscv_ctx) {

    dscv_ctx->sock_tcp_listen = dsm_tcp_bind_listen(dsm_ctx->config.net.node.tcp_port);
    if (dscv_ctx->sock_tcp_listen < 0) {
        return -1;
    }

    spvs_ctx = malloc(sizeof(dsm_supervisor_ctx));
    if (!spvs_ctx) {
        LOG_ERROR("malloc : spvs_ctx");
        return -1;
    }

    if (pthread_mutex_init(&spvs_ctx->eligible_mutex, NULL) != 0 ||
        pthread_mutex_init(&spvs_ctx->poll_update_mutex, NULL) != 0) {
        return -1;
    }

    dsm_list_init(&spvs_ctx->eligible_ip);
    dsm_list_init(&spvs_ctx->poll_fds_update);

    spvs_ctx->pool = pool_create(dsm_ctx->config.net.node.pool_nb_threads,
                                 dsm_ctx->config.net.node.pool_queue_size);

    if (dsm_safe_map_init(&spvs_ctx->spvs_reg, DSM_SPVS_HASH_BITS) < 0) {
        return -1;
    }

    return 0;
}

static void dispatch_call(void *arg) {
    dsm_msg_thread_ctx *msg_ctx = (dsm_msg_thread_ctx *)arg;

    // TODO

    free(msg_ctx->payload);
    free(msg_ctx);
    return;
}

static int handle_udp_mcast(dsm_dscv_ctx *dscv_ctx) {
    LOG_INFO("Message reçu sur la socket UDP multicast");

    dsm_msg_thread_ctx *msg_ctx = malloc(sizeof(dsm_msg_thread_ctx));
    if (!msg_ctx) {
        LOG_ERROR("Erreur malloc : msg_ctx");
        return -1;
    }
    struct timeval tv = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};
    uint8_t hbuf[sizeof(dsm_msg_header)];

    if (dsm_udp_recvfrom(dscv_ctx->sock_udp_mcast, hbuf, sizeof(hbuf),
                         &msg_ctx->src_addr.sin_addr.s_addr, &msg_ctx->src_addr.sin_port,
                         &tv) < 0) {
        goto exit_err_ctx;
    }

    if (dsm_dsrlze_header(hbuf, sizeof(hbuf), &msg_ctx->header) < 0) {
        goto exit_err_ctx;
    }

    msg_ctx->payload = malloc(msg_ctx->header.payload_len);
    if (!msg_ctx->payload) {
        goto exit_err_ctx;
    }

    tv.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (dsm_udp_recvfrom(dscv_ctx->sock_udp_mcast, msg_ctx->payload,
                         msg_ctx->header.payload_len, NULL, NULL, &tv) < 0) {
        goto exit_err_payload;
    }

    if (pool_add_task(spvs_ctx->pool, dispatch_call, msg_ctx) < 0) {
        goto exit_err_payload;
    }

    return 0;

exit_err_payload:
    free(msg_ctx->payload);

exit_err_ctx:
    free(msg_ctx);

    return -1;
}

static int handle_tcp_listen(dsm_dscv_ctx *dscv_ctx) {
    LOG_INFO("Message reçu sur la socket TCP d'écoute");

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_sock =
        accept(dscv_ctx->sock_tcp_listen, (struct sockaddr *)&client_addr, &addr_len);

    if (client_sock < 0) {
        LOG_ERROR("Erreur accept : %d", client_sock);
        return -1;
    }

    uint32_t client_ip = client_addr.sin_addr.s_addr;
    dsm_eligible_entry *pos, *n;
    pthread_mutex_lock(&spvs_ctx->eligible_mutex);
    dsm_list_for_each_entry_safe(pos, n, &spvs_ctx->eligible_ip, node) {
        if (pos->ip == client_ip) {
            dsm_list_del(&pos->node);
            free(pos);
            pthread_mutex_unlock(&spvs_ctx->eligible_mutex);
            LOG_INFO("Client éligible connecté");
            goto found;
        }
    }
    pthread_mutex_unlock(&spvs_ctx->eligible_mutex);
    close(client_sock);
    LOG_ERROR("Connexion d'un client non éligible");
    return -1;

    dsm_poll_update_entry *update_entry;
    unsigned int id_node;
found:

    bool added = increment_if_less_ext(&spvs_ctx->id_count,
                                       dsm_ctx->config.net.system.nb_nodes - 1, &id_node);

    if (!added) {
        close(client_sock);
        // send system full
        return 0;
    }

    update_entry = malloc(sizeof(dsm_poll_update_entry));
    if (!update_entry) {
        LOG_ERROR("Erreur malloc : update_entry");
        return -1;
    }

    update_entry->id_node = id_node;
    update_entry->fd = client_sock;
    pthread_mutex_lock(&spvs_ctx->poll_update_mutex);
    dsm_list_add_tail(&update_entry->node, &spvs_ctx->poll_fds_update);
    pthread_mutex_unlock(&spvs_ctx->poll_update_mutex);
    // send id_node

    return 0;
}

static int handle_tcp_recv(dsm_dscv_ctx *dscv_ctx, int poll_index) {
    unsigned int id_node = poll_index - DSM_SPVS_POLL_FDS_SERVER;
    LOG_INFO("Message reçu sur une socket TCP (ID_Node=%d)", id_node);

    dsm_msg_thread_ctx *msg_ctx = malloc(sizeof(dsm_msg_thread_ctx));
    if (!msg_ctx) {
        LOG_ERROR("Erreur malloc : msg_ctx");
        return -1;
    }

    int recv_sock = -1;
    dsm_supervising_node *node_content;
    dsm_hash_safe_get(&spvs_ctx->spvs_reg, node_content, node, id_node) {
        if (node_content->id == id_node) {
            recv_sock = node_content->tcp_sock;
            break;
        }
    }
    dsm_hash_safe_get_end(&spvs_ctx->spvs_reg, id_node);

    return 0;
}

static void update_spvs_state() {
    dsm_supervising_state state = atomic_load(&spvs_ctx->state);
    unsigned int expected;
    bool is_manager = dsm_ctx->config.net.node.role == DSM_ROLE_MANAGER;

    switch (state) {
        case DSM_SPVS_STATE_SCANNING:
            expected = dsm_ctx->config.net.system.nb_nodes - 1;
            if (atomic_load(&spvs_ctx->join_count) == expected) {
                atomic_store(&spvs_ctx->state, DSM_SPVS_STATE_ROLE_COLLECTING);
                // TODO send id / system full
            }
            return;

        case DSM_SPVS_STATE_ROLE_COLLECTING:
            expected = dsm_ctx->config.net.system.nb_nodes - 1;
            if (atomic_load(&spvs_ctx->join_count) == expected) {
                atomic_store(&spvs_ctx->state, DSM_SPVS_STATE_MGR_SHARING);
                // TODO share registry send
            }
            return;

        case DSM_SPVS_STATE_MGR_SHARING:
            expected = dsm_ctx->config.net.system.nb_managers - (is_manager ? 1 : 0);
            if (atomic_load(&spvs_ctx->share_mgr_count) == expected) {
                atomic_store(&spvs_ctx->state, DSM_SPVS_STATE_MGR_WAITING);
                // TODO mgr_ready send
            }
            return;

        case DSM_SPVS_STATE_MGR_WAITING:
            expected = dsm_ctx->config.net.system.nb_managers - (is_manager ? 1 : 0);
            if (atomic_load(&spvs_ctx->mgr_ready_count) == expected) {
                atomic_store(&spvs_ctx->state, DSM_SPVS_STATE_WKR_CONFIGURING);
                // TODO wkr_config send
            }
            return;

        case DSM_SPVS_STATE_WKR_CONFIGURING:
            expected = dsm_ctx->config.net.system.nb_nodes -
                       dsm_ctx->config.net.system.nb_managers - (is_manager ? 1 : 0);
            if (atomic_load(&spvs_ctx->wkr_config_count) == expected) {
                atomic_store(&spvs_ctx->state, DSM_SPVS_STATE_FINALIZING);
                // TODO finalize send
            }
            return;

        default:
            return;
    }
}

static void update_poll_fds(struct pollfd *fds, int max_fds, int skip_fds) {
    int index;
    dsm_poll_update_entry *pos, *n;
    pthread_mutex_lock(&spvs_ctx->poll_update_mutex);
    dsm_list_for_each_entry_safe(pos, n, &spvs_ctx->poll_fds_update, node) {
        index = pos->id_node + skip_fds;
        if (index >= 0 && index < max_fds) {
            fds[index].fd = pos->fd;
        }
        dsm_list_del(&pos->node);
        free(pos);
    }
    pthread_mutex_unlock(&spvs_ctx->poll_update_mutex);
}

int dsm_supervisor_loop(dsm_dscv_ctx *dscv_ctx) {

    if (supervisor_config(dscv_ctx) < 0) {
        return -1;
    }

    int max_fds = DSM_SPVS_POLL_FDS_SERVER + dsm_ctx->config.net.system.nb_nodes - 1;
    struct pollfd fds[max_fds];

    for (int i = 0; i < max_fds; i++) {
        fds[i].fd = -1;
        fds[i].events = POLLIN;
    }

    fds[0].fd = dscv_ctx->sock_udp_mcast;
    fds[1].fd = dscv_ctx->sock_tcp_listen;
    while (atomic_load(&spvs_ctx->state) != DSM_SPVS_STATE_FINALIZING) {

        update_spvs_state();
        update_poll_fds(fds, max_fds, DSM_SPVS_POLL_FDS_SERVER);

        int ret = poll(fds, max_fds, DSM_SPVS_POLL_TIMEOUT_MS);
        if (ret < 0) {
            LOG_ERROR("Supervisor poll error (%d)", ret);
            goto exit;

        } else if (ret == 0) { // timeout
            continue;
        }

        for (int i = 0; i < max_fds; i++) {
            if (fds[i].revents & POLLIN) {

                if (i == 0) {
                    if (handle_udp_mcast(dscv_ctx) < 0) {
                        goto exit;
                    }
                } else if (i == 1) {
                    if (handle_tcp_listen(dscv_ctx) < 0) {
                        goto exit;
                    }
                } else {
                    if (handle_tcp_recv(dscv_ctx, i) < 0) {
                        goto exit;
                    }
                }
            }
        }
    }

    // TODO CLEAN & FINALIZE

exit:
    return 0;
}