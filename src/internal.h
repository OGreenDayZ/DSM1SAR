#ifndef DSM_INTERNAL_H
#define DSM_INTERNAL_H

#include "dsm.h"

#include "util/concurrent/safe_map.h"
#include "util/hlist.h"

#include <unistd.h>
#include <stdint.h>
#define DSM_NODE_HASH_BITS 4
#define DSM_PAGE_HASH_BITS 12

/* dsm_node_content correspond au contenu d'un noeud du système,
 * ne pas confondre avec le node d'une liste */
typedef struct {
    unsigned int id;            // key : node_id
    int tcp_sock;               // socket de communication avec ce node
    struct dsm_hlist_node node; // "node" pour la liste de la hashmap
} dsm_node_content;

typedef struct {
    // map des pages : page_id -> dsm_page
    dsm_safe_map pages_reg;
} dsm_manager_ctx; 
typedef struct {
    // map des managers : manager_id -> node_id
    dsm_safe_map managerID_to_nodeID; 
    uint64_t range_start; 
} dsm_worker_ctx;
typedef union {
    dsm_manager_ctx manager_ctx;
    dsm_worker_ctx worker_ctx;
} node_ctx; 


typedef struct {
    /** @warning Une fois le discover fini, config devient READ-ONLY */
    dsm_config config;

    unsigned int node_id;

    dsm_safe_map nodes_reg;
    // + mutex ?
    union {
    dsm_manager_ctx manager_ctx;
    dsm_worker_ctx worker_ctx;
    } node_ctx; 
    // à étendre...
} dsm_context;



extern dsm_context *dsm_ctx; // Initialisé dans dsm.c

// Pool de thread : 
// dsm_thread_pool * thread_pool; ;

#endif // DSM_INTERNAL_H
