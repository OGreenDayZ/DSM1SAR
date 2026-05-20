#ifndef DSM_DAEMON_H
#define DSM_DAEMON_H

#include "util/concurrent/thread_pool.h"

#define DSM_DMN_TIMEOUT_POLL 100

/** Lexique
 * @dmn : Daemon
 */

typedef enum {
    DSM_DMN_STATE_ONGOING,
    DSM_DMN_STATE_TERMINATING,
} dsm_deamon_state;

typedef struct {
    _Atomic dsm_deamon_state state; /**
                                     * @brief : atomic enum
                                     * @warning : READ-ONLY pour tous sauf le main thread
                                     */

    dsm_thread_pool *pool;

} dsm_daemon_ctx;

extern dsm_daemon_ctx *dsm_dmn_ctx;

int dsm_daemon_start(void);

#endif // DSM_DAEMON_H