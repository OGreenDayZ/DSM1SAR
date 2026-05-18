#include "daemon.h"

#include <linux/userfaultfd.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <unistd.h>

#include "debug/debug.h"
#include "dsm.h"
#include "internal.h"
#include "network/manager/manager.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "network/worker/worker.h"
#include "util/concurrent/safe_map.h"
#include "util/concurrent/thread_pool.h"

dsm_daemon_ctx *dsm_dmn_ctx = NULL;

static int dmn_ctx_init() {
    LOG_INFO("Initialisation du contexte du daemon ...");

    if (dsm_dmn_ctx) {
        LOG_ERROR("dsm_dmn_ctx déjà initialisé ?!");
        return -1;
    }

    dsm_dmn_ctx = malloc(sizeof(dsm_daemon_ctx));
    if (!dsm_dmn_ctx) {
        LOG_ERROR("malloc : dsm_dmn_ctx");
        return -1;
    }

    atomic_store(&dsm_dmn_ctx->state, DSM_DMN_STATE_ONGOING);

    dsm_dmn_ctx->pool = pool_create(dsm_ctx->config.net.node.pool_nb_threads,
                                    dsm_ctx->config.net.node.pool_queue_size);

    return 0;
}

void dmn_ctx_cleanup() {
    pool_destroy(dsm_dmn_ctx->pool, graceful_shutdown);
    free(dsm_dmn_ctx);
    dsm_dmn_ctx = NULL;
}

static int recv_header(int fd, dsm_msg_header *out) {
    uint8_t buf[sizeof(dsm_msg_header)];
    struct timeval timeout = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};

    int ret = dsm_tcp_recv_all(fd, buf, sizeof(buf), &timeout);
    if (ret < 0)
        return ret;

    return dsm_dsrlze_header(buf, sizeof(buf), out);
}

static int recv_payload(int fd, size_t len, uint8_t *out) {
    struct timeval timeout = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};
    return dsm_tcp_recv_all(fd, out, len, &timeout);
}

static int handle_uffd_event(int uffd) {
    LOG_INFO("Message reçu sur userfaultfd");

    int ret = 0;
    dsm_msg_uffd_thread_ctx *msg_ctx = malloc(sizeof(dsm_msg_uffd_thread_ctx));
    if (!msg_ctx) {
        LOG_ERROR("malloc : msg_ctx");
        return -1;
    }

    memset(msg_ctx, 0, sizeof(dsm_msg_uffd_thread_ctx));

    if (read(uffd, &msg_ctx->fault_msg, sizeof(struct uffd_msg)) < 0) {
        LOG_ERROR("read userfaultfd");
        ret = -1;
        goto exit_err_ctx;
    }

    if (pool_add_task(dsm_dmn_ctx->pool, dispatch_call_uffd, msg_ctx) < 0) {
        ret = -1;
        goto exit_err_ctx;
    }

    return 0;

exit_err_ctx:
    free(msg_ctx);

    return ret;
}

static int handle_socket_event(int recv_sock) {
    LOG_INFO("Message reçu sur socket FD=%d", recv_sock);

    int ret = 0;

    dsm_msg_tcpss_thread_ctx *msg_ctx = malloc(sizeof(dsm_msg_tcpss_thread_ctx));
    if (!msg_ctx) {
        LOG_ERROR("malloc : msg_ctx");
        return -1;
    }

    msg_ctx->sock = recv_sock;

    ret = recv_header(recv_sock, &msg_ctx->header);
    if (ret < 0) {
        goto exit_err_ctx;
    }

    unsigned int node_id = msg_ctx->header.id;
    dsm_node_content *content;
    dsm_hash_safe_get(&dsm_ctx->nodes_reg, content, node, node_id) {
        if (content->id == node_id &&
            content->tcp_sock == recv_sock) { // Pas forcement utile la 2ème condition
            msg_ctx->send_lock = &content->send_lock;
        }
    }
    dsm_hash_safe_get_end(&dsm_ctx->nodes_reg, node_id);

    msg_ctx->payload = malloc(msg_ctx->header.payload_len);
    if (!msg_ctx->payload) {
        LOG_ERROR("malloc : msg_ctx->payload");
        ret = -1;
        goto exit_err_payload;
    }

    ret = recv_payload(recv_sock, msg_ctx->header.payload_len, msg_ctx->payload);
    if (ret < 0) {
        goto exit_err_payload;
    }

    if (dsm_ctx->config.net.node.role == DSM_ROLE_MANAGER) {

        if (pool_add_task(dsm_dmn_ctx->pool, dispatch_call_mgr, msg_ctx) < 0) {
            ret = -1;
            goto exit_err_payload;
        }
    } else {
        if (pool_add_task(dsm_dmn_ctx->pool, dispatch_call_wkr, msg_ctx) < 0) {
            ret = -1;
            goto exit_err_payload;
        }
    }

    return 0;

exit_err_payload:
    free(msg_ctx->payload);

exit_err_ctx:
    free(msg_ctx);

    return ret;
}

static int update_dmn_state() {
    // TODO
    return 0;
}

int dsm_daemon_start(void) {
    int ret = 0;

    dsm_node_role my_role = dsm_ctx->config.net.node.role;
    dsm_system_config *sys_cfg = &dsm_ctx->config.net.system;
    unsigned int nb_workers = sys_cfg->nb_nodes - sys_cfg->nb_managers;

    int max_fds;
    if (my_role == DSM_ROLE_MANAGER) {
        max_fds = nb_workers;
    } else {
        max_fds = sys_cfg->nb_managers + 1;
    }

    struct pollfd fds[max_fds];
    int index = 0;
    if (my_role == DSM_ROLE_WORKER) {
        fds[index] = (struct pollfd){.fd = dsm_wkr_ctx->uffd, .events = POLLIN};
        index++;
    }

    unsigned int bkt;
    dsm_node_content *pos;

    dsm_hash_safe_for_each(&dsm_ctx->nodes_reg, bkt, pos, node) {
        fds[index] = (struct pollfd){.fd = pos->tcp_sock, .events = POLLIN};
        index++;
    }

    if (dmn_ctx_init() < 0) {
        ret = -1;
        goto exit;
    }

    while (atomic_load(&dsm_dmn_ctx->state) != DSM_DMN_STATE_TERMINATING) {
        if (update_dmn_state() < 0) {
            ret = -1;
            goto exit;
        }

        int n = poll(fds, max_fds, DSM_DMN_TIMEOUT_POLL);

        if (n == 0)
            continue; /* timeout */

        if (n < 0) {
            LOG_ERROR("poll");
            ret = -1;
            goto exit;
        }

        for (int i = 0; i < max_fds; i++) {
            if (fds[i].revents & POLLIN) {

                if (my_role == DSM_ROLE_WORKER && i == 0) {
                    if (handle_uffd_event(fds[i].fd) < 0) {
                        continue;
                    }
                } else {
                    ret = handle_socket_event(fds[i].fd);
                    if (ret < 0) {
                        if (ret == DSM_ERR_TCP_CONN_CLOSE) {
                            LOG_WARNING("Connexion fermée, màj du poll, FD=%d -> -1",
                                        fds[i].fd);
                            fds[i].fd = -1;
                        }
                        continue;
                    }
                }
            }
        }
    }
    ret = 0;

exit:
    dmn_ctx_cleanup();
    return ret;
}
