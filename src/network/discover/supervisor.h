#ifndef DSM_SUPERVISOR_H
#define DSM_SUPERVISOR_H

#include <stdatomic.h>
#include <stdbool.h>

#include "network/discover/net_discover.h"

#define DSM_SPVS_POLL_MAX_FDS 2
#define DSM_SPVS_POLL_TIMEOUT_MS 3000

typedef enum {
    DSM_SPVS_STATE_SCANNING,    /* [CODE_DISCOVER_INIT -> CODE_DISCOVER_SYSTEM_FULL] */
    DSM_SPVS_STATE_SHARING,     /* [CODE_DISCOVER_SHARE_REGISTRY_MANAGER ->
                                   CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK] */
    DSM_SPVS_STATE_CONFIGURING, /* [CODE_DISCOVER_MANAGER_READY ->
                                CODE_DISCOVER_WORKER_CONFIG_ACK] */
    DSM_SPVS_STATE_FINALIZING,  /* [CODE_DISCOVER_FINALIZE] */
} dsm_supervising_state;

typedef struct {
    bool finalized;

    atomic_int id_count;
    pthread_mutex_t id_count_mutex;

    // JOIN LIST ?
    // reg temp
    // pool threads

} dsm_supervisor_ctx;

int dsm_supervisor_loop(dsm_dscv_ctx *dscv_ctx);

#endif // DSM_SUPERVISOR_H
