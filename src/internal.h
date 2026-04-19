#ifndef DSM_INTERNAL_H
#define DSM_INTERNAL_H

#include "dsm.h"

#include "util/concurrent/safe_map.h"
#include "util/hlist.h"

#define DSM_NODE_HASH_BITS 4
#define DSM_PAGE_HASH_BITS 12

/* dsm_node_content correspond au contenu d'un noeud du système,
 * ne pas confondre avec le node d'une liste */
typedef struct {
    unsigned int id; // key : node_id
    int tcp_sock;    // socket de communication avec ce node
    uint32_t ip;     // adresse IP du node (format binaire)
    // TODO REMOVE ip -> candidate_ctx
    struct dsm_hlist_node node; // "node" pour la liste de la hashmap
} dsm_node_content;

typedef struct {
    unsigned int id; // key : page_id
    // TODO
    struct dsm_hlist_node node; // "node" pour la liste de la hashmap
} dsm_page_content;

typedef struct {
    /** @warning Une fois le discover fini, config devient READ-ONLY */
    dsm_config config;

    unsigned int node_id;

    dsm_safe_map nodes_reg;
    dsm_safe_map pages_reg;
    // + mutex ?

    // à étendre...
} dsm_context;

extern dsm_context *dsm_ctx; // Initialisé dans dsm.c

#endif // DSM_INTERNAL_H
