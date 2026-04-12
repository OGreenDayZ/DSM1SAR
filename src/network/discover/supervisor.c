#include "supervisor.h"

#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#include "debug/debug.h"
#include "internal.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "network/network.h"

static dsm_supervisor_ctx spvs_ctx;

static int supervisor_config(dsm_dscv_ctx *dscv_ctx) {

    dscv_ctx->sock_tcp_listen = dsm_tcp_bind_listen(dsm_ctx->config.net.node.tcp_port);
    if (dscv_ctx->sock_tcp_listen < 0) {
        return -1;
    }

    if (pthread_mutex_init(&spvs_ctx.id_count_mutex, NULL) != 0) {
        return -1;
    }

    spvs_ctx.pool = pool_create(dsm_ctx->config.net.node.pool_nb_threads,
                                dsm_ctx->config.net.node.pool_queue_size);

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

    if (pool_add_task(spvs_ctx.pool, dispatch_call, msg_ctx) < 0) {
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

    return 0;
}

static int handle_tcp_recv(dsm_msg_thread_ctx *msg_ctx, int poll_index) {
    int id_node = poll_index - DSM_SPVS_POLL_FDS_SERVER;

    return 0;
}

static int update_ctx_state() {
    return 0;
}

static int update_poll_fds() {
    return 0;
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
    // TODO poll update queue
    while (spvs_ctx.state != DSM_SPVS_STATE_FINALIZING) {

        if (update_ctx_state() < 0) {
            goto exit;
        }

        if (update_poll_fds() < 0) {
            goto exit;
        }

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
                    if (handle_tcp_recv(NULL, i) < 0) {
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