#ifndef DSM_SUPERVISOR_H
#define DSM_SUPERVISOR_H

#include <stdatomic.h>

#include "dsm.h"
#include "network/discover/net_discover.h"
#include "util/concurrent/safe_list.h"
#include "util/concurrent/safe_map.h"
#include "util/thread_pool.h"

#define DSM_SPVS_POLL_FDS_SERVER 2
#define DSM_SPVS_POLL_TIMEOUT_MS 1500

#define DSM_SPVS_HASH_BITS 4

typedef enum {
    DSM_SPVS_STATE_SCANNING, /* [CODE_DISCOVER_INIT -> CODE_DISCOVER_ROLE_WISH] */

    DSM_SPVS_STATE_MGR_SHARING, /* [CODE_DISCOVER_SHARE_REGISTRY_MANAGER ->
                               CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK] */

    DSM_SPVS_STATE_MGR_WAITING, /* [CODE_DISCOVER_SHARE_REGISTRY_WORKER ->
                                   CODE_DISCOVER_MANAGER_READY] */

    DSM_SPVS_STATE_WKR_CONFIGURING, /* [CODE_DISCOVER_WORKER_CONFIG ->
                                       CODE_DISCOVER_WORKER_CONFIG_ACK] */

    DSM_SPVS_STATE_FINALIZING, /* [CODE_DISCOVER_FINALIZE] */
} dsm_supervising_state;

/**
 * @note : struct de transmission au thread, utilitaire
 */
typedef struct {
    int sock;
    uint32_t client_ip;
} dsm_eligible_ctx;

typedef struct {
    uint32_t ip;
    struct dsm_list_head node;
} dsm_eligible_entry; // Dans eligible_ip

typedef struct {
    int id_node;
    int fd; // -1 ou nouvelle valeur de fd
    struct dsm_list_head node;
} dsm_poll_update_entry; // Dans poll_fds_update

typedef struct {
    unsigned int id;
    int tcp_sock;

    uint32_t ip;
    uint16_t manager_port;
    dsm_node_role role;

    struct dsm_hlist_node node;
} dsm_supervising_node; // Dans spvs_reg

typedef struct {
    _Atomic dsm_supervising_state
        state; /**
                * @brief : atomic enum
                * @warning : READ-ONLY pour tous sauf le main thread
                */

    atomic_uint id_count;

    dsm_safe_list eligible_ip;
    dsm_safe_list poll_fds_update;

    dsm_thread_pool *pool;

    /**
     * @note : techniquement dans notre implémentation id_count == join_count,
     * mais on sépare pour la maintenabilité du code
     */
    atomic_uint join_count;       // nb_send : CODE_DISCOVER_JOIN_ACK
    atomic_uint role_wish_count;  // nb_recv : CODE_DISCOVER_ROLE_WISH
    atomic_uint share_mgr_count;  // nb_recv : CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK
    atomic_uint mgr_ready_count;  // nb_recv : CODE_DISCOVER_MANAGER_READY
    atomic_uint wkr_config_count; // nb_recv : CODE_DISCOVER_WORKER_CONFIG_ACK

    dsm_safe_map spvs_reg;
} dsm_supervisor_ctx;

int dsm_supervising_start(dsm_discover_ctx *dscv_ctx);

#endif // DSM_SUPERVISOR_H
