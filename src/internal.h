#ifndef DSM_INTERNAL_H
#define DSM_INTERNAL_H

#include <stdint.h>
#include <unistd.h>

#include "dsm.h"
#include "util/concurrent/safe_map.h"
#include "util/hlist.h"

#define DSM_NODE_HASH_BITS 4

/* dsm_node_content correspond au contenu d'un noeud du système,
 * ne pas confondre avec le node d'une liste */
typedef struct {
    unsigned int id; // key : node_id
    /**
     * @warning : C'est un lock uniquement pour send,
     * car seul le daemon n'a le droit de recv
     */
    pthread_mutex_t send_lock;
    int tcp_sock;               // socket de communication avec ce node
    struct dsm_hlist_node node; // "node" pour la liste de la hashmap nodes_reg
} dsm_node_content;

typedef struct {
    /** @warning Une fois le discover fini, config devient READ-ONLY */
    dsm_config config;

    unsigned int node_id;

    dsm_safe_map nodes_reg;

} dsm_context;

typedef struct {
    bool can_restart;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} dsm_restart_ctx;

extern dsm_context *dsm_ctx; // Initialisé dans dsm.c
extern dsm_restart_ctx restart_ctx;

/** ALIAS */

/** Taille effective d'une page (en octets), lue depuis le contexte DSM. */
#define EFFECTIVE_NB_PAGES dsm_ctx->config.pager.nb_pages
/** Nombre effectif de pages, lu depuis le contexte DSM. */
#define EFFECTIVE_PAGESIZE dsm_ctx->config.pager.page_size

#endif // DSM_INTERNAL_H
