#ifndef DSM_MANAGER_H
#define DSM_MANAGER_H

#include "network/message/message.h"

#include "util/concurrent/safe_list.h"
#include "util/concurrent/safe_map.h"

#define DSM_MGR_PAGE_HASH_BITS 12

/**
 * @brief Mode de détention d'une page par son propriétaire courant.
 *
 * Décrit les droits avec lesquels le propriétaire détient la page.
 * Le manager met à jour ce champ à chaque transfert de propriété.
 */
typedef enum {
    PAGE_R,
    PAGE_RW,
    PAGE_LOCKED
} dsm_page_mode_poss;

/**
 * Entrée de la liste des workers détenant une copie d'une page.
 *
 * Chaînée via `dsm_page_content.workers`.
 *
 * @param worker_id  Identifiant du worker concerné.
 * @param node       Nœud d'ancrage dans la liste `dsm_page_content.workers`.
 */
typedef struct {
    int worker_id;
    struct dsm_list_head node;
} reader_info; // liste des workers qui ont la page (Pour "readers")

typedef struct {
    // WIP !!!

    // unsigned int node_id; // author
    // page_mode_req mode;
    int sock;              // callback
    dsm_msg_header header; // callback
    dsm_msg_wkr_req req;

} dsm_request_content; // Pour dsm_request_node

typedef struct {
    // WIP !!!

    dsm_request_content req;
    bool wait_ack;

    struct dsm_list_head node;
} dsm_request_node; // Pour pending_reqs

/**
 * @brief Entrée de la table des pages gérées par un manager.
 *
 * Une instance de @c dsm_page_content est créée par le manager lors du premier
 * accès en écriture à une page. Elle est stockée dans @ref pages_reg
 * (hashmap indexée par @c id) et conservée jusqu'à la fin de la session.
 */
typedef struct {

    unsigned int page_id;    /**< Identifiant de la page (clé de hashmap). */
    dsm_page_mode_poss mode; /**< Mode de possession par le propriétaire actuel. */
    int owner_id;            /**< ID du nœud propriétaire (-1 si aucun). */
    dsm_safe_list readers;   /**< Liste des workers ayant une copie en lecture
                              *   (@ref reader_info). */

    dsm_safe_list pending_reqs; /**< Liste des requêtes en attente sur cette page */
    // TODO ALGO

    struct dsm_hlist_node node; /**< Nœud d'ancrage dans la hashmap @ref pages_reg. */
} dsm_page_content;             // Pour pages_reg

typedef struct {

    // map des pages : page_id -> dsm_page_content
    dsm_safe_map pages_reg;

} dsm_manager_ctx;

extern dsm_manager_ctx *dsm_mgr_ctx;

int dsm_mgr_ctx_init();
void dsm_mgr_ctx_cleanup();

void dispatch_call_mgr(void *arg);

#endif // DSM_MANAGER_H