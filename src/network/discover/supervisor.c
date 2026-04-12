#include "supervisor.h"

#include <poll.h>
#include <pthread.h>

#include "debug/debug.h"
#include "internal.h"
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

    return 0;
}

static int spvs_handle_udp_mcast(dsm_dscv_ctx *dscv_ctx) {
    // TODO
    return 0;
}

static int spvs_handle_tcp_listen(dsm_dscv_ctx *dscv_ctx) {
    // TODO
    return 0;
}

int dsm_supervisor_loop(dsm_dscv_ctx *dscv_ctx) {

    if (supervisor_config(dscv_ctx) < 0) {
        return -1;
    }

    struct pollfd fds[DSM_SPVS_POLL_MAX_FDS];
    fds[0].fd = dscv_ctx->sock_udp_mcast;
    fds[0].events = POLLIN;
    fds[1].fd = dscv_ctx->sock_tcp_listen;
    fds[1].events = POLLIN;

    while (!spvs_ctx.finalized) {
        int ret = poll(fds, DSM_SPVS_POLL_MAX_FDS, DSM_SPVS_POLL_TIMEOUT_MS);
        if (ret < 0) {
            LOG_ERROR("Supervisor poll error (%d)", ret);
            goto exit;

        } else if (ret == 0) { // timeout
            continue;
        }

        if (fds[0].revents & POLLIN) {
            if (spvs_handle_udp_mcast(dscv_ctx) < 0) {
                goto exit;
            }
        }

        if (fds[1].revents & POLLIN) {
            if (spvs_handle_tcp_listen(dscv_ctx) < 0) {
                goto exit;
            }
        }
    }

exit:
    return 0; // TODO
}