#ifndef DSM_MESSAGE_H
#define DSM_MESSAGE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

#define DSM_MSG_RECV_TIMEOUT_SEC 5

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
} dsm_msg_code;

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
    uint32_t role;
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
// } dsm_msg_dscv_sys_full;

/* Composant de dsm_msg_dscv_share_reg */
typedef struct {
    uint32_t id;
    uint8_t is_manager;
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
} dsm_msg_dscv_wkr_config;

/* CODE_DISCOVER_WORKER_CONFIG_ACK */
// typedef struct {
//     // struct vide
// } dsm_msg_dscv_wkr_config_ack;

/* CODE_DISCOVER_FINALIZE */
// typedef struct {
//     // struct vide
// } dsm_msg_dscv_finalize;

#pragma pack(pop)

int dsm_tcp_send_all(int sock, const uint8_t *buf, size_t len);
int dsm_tcp_recv_all(int sock, uint8_t *buf, size_t len, struct timeval *tv);
int dsm_udp_sendto(int sock, const uint8_t *buf, size_t len, const char *dest_ip,
                   int dest_port);
int dsm_udp_recvfrom(int sock, uint8_t *buf, size_t len, char *src_ip, int *src_port,
                     struct timeval *tv);

#endif // DSM_MESSAGE_H