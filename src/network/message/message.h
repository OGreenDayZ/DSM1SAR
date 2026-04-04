#ifndef DSM_MESSAGE_H
#define DSM_MESSAGE_H

#include <stdint.h>

typedef enum {
    /* Discover */
    CODE_DISCOVER_INIT = 1,
    CODE_DISCOVER_SUPERVISOR_DATA = 2,
    CODE_DISCOVER_JOIN_ACK = 3,
    CODE_DISCOVER_SYSTEM_FULL = 4,
    CODE_DISCOVER_SHARE_REGISTRY_MANAGER = 5,
    CODE_DISCOVER_SHARE_REGISTRY_WORKER = 6,
    CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK = 7,
    CODE_DISCOVER_MANAGER_READY = 8,
    CODE_DISCOVER_WORKER_CONFIG = 9,
    CODE_DISCOVER_WORKER_CONFIG_ACK = 10,
    CODE_DISCOVER_FINALIZE = 11,

    /* +... */
} dsm_msg_code_t;

#pragma pack(push, 1)
// Au lieu de __attribute__((packed)) chaque struct

/* HEADER */
typedef struct {
    uint32_t type;
    uint32_t id; // SystemID au début et NodeID ensuite
    uint32_t payload_len;
} dsm_msg_header_t;

/* CODE_DISCOVER_INIT */
typedef struct {
    uint16_t udp_port_uni;
    uint32_t role;
} dsm_msg_dscv_init_t;

/* CODE_DISCOVER_SUPERVISOR_DATA */
typedef struct {
    uint16_t tcp_port_spvs;
} dsm_msg_dscv_spvs_data_t;

/* CODE_DISCOVER_JOIN_ACK */
typedef struct {
    uint32_t assign_id;
} dsm_msg_dscv_join_ack_t;

/* CODE_DISCOVER_SYSTEM_FULL */
// typedef struct {
//     // struct vide
// } dsm_msg_dscv_sys_full_t;

/* Composant de dsm_msg_dscv_share_reg_t */
typedef struct {
    uint32_t id;
    uint8_t is_manager;
    uint32_t ip;
    uint16_t manager_port;
} dsm_reg_node_t;

/* CODE_DISCOVER_SHARE_REGISTRY_MANAGER
+ CODE_DISCOVER_SHARE_REGISTRY_WORKER*/
typedef struct {
    uint32_t nb_nodes;
    dsm_reg_node_t nodes[];
} dsm_msg_dscv_share_reg_t;

/* CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK */
typedef struct {
    uint16_t listen_port;
} dsm_msg_dscv_share_reg_ack_t;

/* CODE_DISCOVER_MANAGER_READY */
// typedef struct {
//     // struct vide
// } dsm_msg_dscv_mgr_ready_t;

/* CODE_DISCOVER_WORKER_CONFIG */
typedef struct {
    uint64_t nb_pages;
} dsm_msg_dscv_wkr_config_t;

/* CODE_DISCOVER_WORKER_CONFIG_ACK */
// typedef struct {
//     // struct vide
// } dsm_msg_dscv_wkr_config_ack_t;

/* CODE_DISCOVER_FINALIZE */
// typedef struct {
//     // struct vide
// } dsm_msg_dscv_finalize_t;

#pragma pack(pop)

#endif // DSM_MESSAGE_H