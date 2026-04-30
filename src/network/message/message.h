#ifndef DSM_MESSAGE_H
#define DSM_MESSAGE_H

#include <linux/userfaultfd.h>
#include <netinet/in.h>
#include <sys/types.h>

#define DSM_MSG_RECV_TIMEOUT_SEC 5
#define DSM_MTU_SIZE 1500
#define DSM_ERR_TCP_CONN_CLOSE -2

typedef enum {
    /* Discover */
    CODE_DISCOVER_INIT = 1,
    CODE_DISCOVER_SUPERVISOR_DATA,
    CODE_DISCOVER_JOIN_ACK,
    CODE_DISCOVER_SYSTEM_FULL,
    CODE_DISCOVER_ROLE_WISH,
    CODE_DISCOVER_SHARE_REGISTRY_MANAGER,
    CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK,
    CODE_DISCOVER_SHARE_REGISTRY_WORKER,
    CODE_DISCOVER_WORKER_IDENTITY,
    CODE_DISCOVER_WORKER_IDENTITY_ACK,
    CODE_DISCOVER_MANAGER_READY,
    CODE_DISCOVER_WORKER_CONFIG,
    CODE_DISCOVER_WORKER_CONFIG_ACK,
    CODE_DISCOVER_FINALIZE,

    /* Pager / Daemon */
    CODE_WKR_REQ,
    CODE_WKR_DATA_ACK,
    CODE_WKR_INVA_ACK,

    CODE_MGR_REQ_ACK,
    CODE_MGR_DATA_REQ,
    CODE_MGR_INVA_REQ,

    /* Synchro */
    /*
     * WKR : BARRIER_REQ, FINALIZE_REQ
     * MGR : BARRIER_ACK, FINALIZE_ACK
     */
    CODE_SYNC_BARRIER_REQ,
    CODE_SYNC_BARRIER_ACK,
    CODE_SYNC_FINALIZE_REQ,
    CODE_SYNC_FINALIZE_ACK

} dsm_msg_code;

/** Lexique
 * @dscv: discover
 * @spvs: supervisor
 * @reg: registry
 * @mgr: manager
 * @wkr: worker
 * @pgr: pager
 * @inva: invalidate
 * @barr: barrier
 * @fina: finalize
 */

#pragma pack(push, 1)
// Au lieu de __attribute__((packed)) chaque struct

/* HEADER */
typedef struct {
    uint32_t code;
    uint32_t id; // SystemID au début et NodeID ensuite
    uint32_t payload_len;
} dsm_msg_header;

/* CODE_DISCOVER_INIT */
typedef struct {
    uint16_t udp_port_uni;
} dsm_msg_dscv_init;

/* CODE_DISCOVER_SUPERVISOR_DATA */
typedef struct {
    uint16_t tcp_port_spvs;
} dsm_msg_dscv_spvs_data;

/* CODE_DISCOVER_JOIN_ACK */
typedef struct {
    uint32_t assigned_id;
} dsm_msg_dscv_join_ack;

/* CODE_DISCOVER_SYSTEM_FULL */
// typedef struct {
//     // struct vide
// } dsm_msg_dscv_sys_full_t;

/* CODE_DISCOVER_ROLE_WISH */
typedef struct {
    uint32_t role;
} dsm_msg_dscv_role_wish;

/* Composant de dsm_msg_dscv_share_reg_t */
typedef struct {
    uint32_t id;
    uint32_t ip;
    uint16_t manager_port;
} dsm_reg_node;

/* CODE_DISCOVER_SHARE_REGISTRY_MANAGER
+ CODE_DISCOVER_SHARE_REGISTRY_WORKER*/
typedef struct {
    uint32_t sys_total_nodes; // la taille totale du système (manager + workers)
    uint32_t nb_nodes; // le nombre de nodes envoyés dans ce message (< sys_total_nodes)
    dsm_reg_node nodes[];
} dsm_msg_dscv_share_reg;

/* CODE_DISCOVER_WORKER_IDENTITY */
// typedef struct {
//     // struct vide
// } dsm_msg_dscv_wkr_id;

/* CODE_DISCOVER_WORKER_IDENTITY_ACK */
// typedef struct {
//     // struct vide
// } dsm_msg_dscv_wkr_id_ack;

/* CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK */
typedef struct {
    uint16_t listen_port;
} dsm_msg_dscv_share_reg_ack;

/* CODE_DISCOVER_MANAGER_READY */
// typedef struct {
//     // struct vide
// } dsm_msg_dscv_mgr_ready;

/* CODE_DISCOVER_WORKER_CONFIG */
typedef struct {
    uint64_t nb_pages;
    uint32_t page_size;
} dsm_msg_dscv_wkr_config;

/* CODE_DISCOVER_WORKER_CONFIG_ACK */
// typedef struct {
//     // struct vide
// } dsm_msg_dscv_wkr_config_ack;

/* CODE_DISCOVER_FINALIZE */
// typedef struct {
//     // struct vide
// } dsm_msg_dscv_finalize;

/* PAGER */
typedef struct {
    uint32_t page_id;
    /**
     * @warning est en réalité un enum @c page_mode_req
     * Lors de la serialisation et deserialisation un cast est effectué from uint8_t ->
     * uint32_t Attention a ne pas dépasser 255 et a toujours mettre des entiers non
     * signés.
     */
    uint8_t mode;
} dsm_msg_wkr_req;

typedef struct {
    uint32_t page_id;
    uint8_t is_sync;
} dsm_msg_mgr_inva_req;

typedef struct {
    uint32_t page_id;
    uint8_t is_invalidate;
} dsm_msg_mgr_data_req;
typedef struct {
    uint32_t page_id;
} dsm_msg_wkr_inva_ack;

typedef struct {
    uint32_t page_id;
    // Pas de champs data_info, car si on lui demande des données alors il est le
    // propriétaire
    uint8_t data[];
} dsm_msg_wkr_data_ack;

typedef struct {
    uint32_t page_id;
    /**
     * @warning est en réalité un enum @c page_mode_req
     * Lors de la serialisation et deserialisation un cast est effectué from uint8_t ->
     * uint32_t Attention a ne pas dépasser 255 et a toujours mettre des entiers non
     * signés.
     */
    uint8_t req_mode;
    /**
     * @warning est en réalité un enum @c data_info
     * Lors de la serialisation et deserialisation un cast est effectué from uint8_t ->
     * uint32_t Attention a ne pas dépasser 255 et a toujours mettre des entiers non
     * signés.
     */
    uint8_t data_info;
    uint8_t data[];
} dsm_msg_mgr_req_ack;

/* CODE_SYNC_BARRIER_REQ */
// typedef struct {
//     // struct vide
// } dsm_msg_sync_barr_req;

/* CODE_SYNC_BARRIER_ACK */
// typedef struct {
//     // struct vide
// } dsm_msg_sync_barr_ack;

/* CODE_SYNC_FINALIZE_REQ */
// typedef struct {
//     // struct vide
// } dsm_msg_sync_fina_req;

/* CODE_SYNC_FINALIZE_ACK */
// typedef struct {
//     // struct vide
// } dsm_msg_sync_fina_ack;

#pragma pack(pop)

/** @note Contexte pour les threads créés lors du DISCOVERY */

/**
 * @note : struct de transmission au thread
 */
typedef struct {
    struct sockaddr_in src_addr; // callback UDP
    dsm_msg_header header;
    uint8_t *payload;
} dsm_msg_udp_thread_ctx;

/**
 * @note : struct de transmission au thread
 */
typedef struct {
    int sock; // callback TCP
    dsm_msg_header header;
    uint8_t *payload;
} dsm_msg_tcp_thread_ctx;

/** @note Contexte pour les threads créés par le daemon */

/**
 * @note : struct de transmission au thread
 */
typedef struct {
    struct uffd_msg fault_msg;
} dsm_msg_uffd_thread_ctx;

/**
 * @note : struct de transmission au thread
 * @note : tcpss == TCP Send Sync
 */
typedef struct {
    int sock;                   // callback TCP
    pthread_mutex_t *send_lock; // callback mutex
    dsm_msg_header header;
    uint8_t *payload;
} dsm_msg_tcpss_thread_ctx;

int dsm_tcp_send_all(int sock, const uint8_t *buf, size_t len);
int dsm_tcp_recv_all(int sock, uint8_t *buf, size_t len, struct timeval *tv);
int dsm_udp_sendto(int sock, const uint8_t *buf, size_t len, uint32_t dest_ip,
                   uint16_t dest_port);
int dsm_udp_recvfrom(int sock, uint8_t *buf, size_t len, uint32_t *src_ip,
                     uint16_t *src_port, struct timeval *tv);

#endif // DSM_MESSAGE_H