#ifndef DSM_MESSAGE_H
#define DSM_MESSAGE_H

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

#define DSM_MSG_RECV_TIMEOUT_SEC 5
#define DSM_MTU_SIZE 1500

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

    CODE_PAGER_REQUEST, // worker
    CODE_PAGER_INVALIDATE, // manager
    CODE_PAGER_DATA_REQUEST, // manager
    CODE_PAGER_DATA, // worker ou manager
} dsm_msg_code;

/** Lexique
 * @dscv: discover
 * @spvs: supervisor
 * @reg: registry
 * @mgr: manager
 * @wkr: worker
 * @pgr : pager
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
    uint8_t mode; // 0 : read, 1 : write
} dsm_msg_pgr_wkr_req;

typedef struct {
    uint32_t page_id;
} dsm_msg_pgr_mgr_inval;

typedef struct {
    uint32_t page_id;
} dsm_msg_pgr_mgr_data_req;

typedef struct {
    uint32_t page_id;
    uint8_t data[]; // la page entière
} dsm_msg_pgr_data;



#pragma pack(pop)

/**
 * @note : struct de transmission au thread
 */
typedef struct {
    struct sockaddr_in src_addr;
    dsm_msg_header header;
    uint8_t *payload;
} dsm_msg_udp_thread_ctx;

/**
 * @note : struct de transmission au thread
 */
typedef struct {
    int sock;
    dsm_msg_header header;
    uint8_t *payload;
} dsm_msg_tcp_thread_ctx;

int dsm_tcp_send_all(int sock, const uint8_t *buf, size_t len);
int dsm_tcp_recv_all(int sock, uint8_t *buf, size_t len, struct timeval *tv);
int dsm_udp_sendto(int sock, const uint8_t *buf, size_t len, uint32_t dest_ip,
                   uint16_t dest_port);
int dsm_udp_recvfrom(int sock, uint8_t *buf, size_t len, uint32_t *src_ip,
                     uint16_t *src_port, struct timeval *tv);

#endif // DSM_MESSAGE_H