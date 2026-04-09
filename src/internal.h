#ifndef DSM_INTERNAL_H
#define DSM_INTERNAL_H

#include "dsm.h"

#include "util/hashmap.h"

/* dsm_node_content correspond au contenu d'un noeud du système,
 * ne pas confondre avec le node d'une liste */
typedef struct {
    unsigned int id;            // key : node_id
    int tcp_sock;               // socket de communication avec ce node
    struct dsm_hlist_node node; // "node" pour la liste de la hashmap
} dsm_node_content;

typedef struct {
    unsigned int id; // key : page_id
    // TODO
    struct dsm_hlist_node node; // "node" pour la liste de la hashmap
} dsm_page_content;

DSM_DEFINE_HASHTABLE(registry_nodes, 4); // TODO : value initial size

typedef struct {
    /* Une fois le discover fini, config devient READ-ONLY */
    dsm_config config;
    unsigned int node_id;
    // registry_nodes
    // registry_pages

    // à étendre...
} dsm_context;
// annuaire des pages -> hashmap

extern dsm_context *dsm_ctx;

#endif // DSM_INTERNAL_H
