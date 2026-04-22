#ifndef DSM_INTERNAL_H
#define DSM_INTERNAL_H

#include "dsm.h"

#include "util/hlist.h"

#define DSM_NODE_HASH_BITS 4
#define DSM_PAGE_HASH_BITS 12

/* dsm_node_content correspond au contenu d'un noeud du système,
 * ne pas confondre avec le node d'une liste */
typedef struct {
    unsigned int id;            // key : node_id
    int tcp_sock;               // socket de communication avec ce node
    uint32_t ip;                // adresse IP du node (format binaire)
    struct dsm_hlist_node node; // "node" pour la liste de la hashmap
} dsm_node_content;

typedef struct {
    // map des pages détenues par le worker, indexé par page_id
    struct dsm_hlist_head pages_reg[1 << DSM_PAGE_HASH_BITS];
} dsm_manager_ctx; 

typedef struct {
    // map des pages et par quel manager ils sont detenus, indexé par page_id
} dsm_worker_ctx;
typedef struct {
    /* Une fois le discover fini, config devient READ-ONLY */
    dsm_config config;

    unsigned int node_id;

    struct dsm_hlist_head nodes_reg[1 << DSM_NODE_HASH_BITS];
    // struct dsm_hlist_head pages_reg[1 << DSM_PAGE_HASH_BITS];
    // + mutex ?
    dsm_manager_ctx manager_ctx;
    dsm_worker_ctx worker_ctx;  

    // à étendre...
} dsm_context;



extern dsm_context *dsm_ctx; // Initialisé dans dsm.c

#endif // DSM_INTERNAL_H
