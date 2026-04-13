#ifndef DSM_SUPERVISOR_H
#define DSM_SUPERVISOR_H

#include <stdatomic.h>
#include <stdbool.h>

#include "network/discover/net_discover.h"
#include "util/list.h"
#include "util/thread_pool.h"

#define DSM_SPVS_POLL_FDS_SERVER 2
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
    int ip;
    struct dsm_list_head node;
} dsm_eligible_entry;

typedef struct {
    int id_node;
    int fd;
    struct dsm_list_head node;
} dsm_poll_update_entry;

typedef struct {
    dsm_supervising_state state;

    atomic_int id_count;
    pthread_mutex_t id_count_mutex;

    dsm_list_head eligible_ip;
    pthread_mutex_t eligible_mutex;

    // TODO + map reg temp

    dsm_list_head poll_fds_update;
    pthread_mutex_t poll_update_mutex;

    dsm_thread_pool *pool;

    atomic_int ack_count;
    pthread_mutex_t ack_count_mutex;

} dsm_supervisor_ctx;

int dsm_supervisor_loop(dsm_dscv_ctx *dscv_ctx);

#endif // DSM_SUPERVISOR_H
