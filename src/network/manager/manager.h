#ifndef DSM_MANAGER_H
#define DSM_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#include "network/message/message.h"
#include "util/concurrent/drain_queue.h"
#include "util/concurrent/safe_list.h"
#include "util/concurrent/safe_map.h"
#include "util/hlist.h"
#include "util/list.h"

#define DSM_MGR_PAGE_HASH_BITS 12

/**
 * @brief Mode de détention d'une page par son propriétaire courant.
 *
 * Décrit les droits avec lesquels le propriétaire détient la page.
 * Le manager met à jour ce champ à chaque transfert de propriété.
 */
typedef enum {
    PAGE_INIT, // page nouvellement créée, jamais reçu/traitée de requête dessus
    PAGE_R,
    PAGE_RW,
    PAGE_LOCKED
} page_mode_poss;

/**
 * Entrée de la liste des workers détenant une copie d'une page.
 *
 * Chaînée via `dsm_page_content.workers`.
 *
 * @param worker_id  Identifiant du worker concerné.
 * @param node       Nœud d'ancrage dans la liste `dsm_page_content.workers`.
 */
typedef struct {
    unsigned int worker_id;
    struct dsm_list_head node;
} dsm_reader_info; // Pour readers

typedef struct {

    dsm_msg_header header; // callback
    dsm_msg_wkr_req info;

    dsm_dq_node node;
} dsm_request_content; // Pour pending_reqs

typedef struct { // Reset à chaque nouveau traitement de requête (en tête de la queue)

    pthread_mutex_t lock; /** Obligatoire pour chaque write (sauf drainer avec le front),
                           * déterminera l'unique sender-finaliseur */

    unsigned int missing_acks; // somme data + inva
    bool promotion_only;       // utilisé quand data_size == 0
    size_t data_size;
    uint8_t *data;

} pending_reqs_head_ctx; // Pour dsm_page_content

typedef struct {

    unsigned int page_id; /**< Identifiant de la page (clé de hashmap). */
    page_mode_poss mode;  /**< Mode de possession par le propriétaire actuel. */

    int owner_id;          /**< ID du nœud propriétaire (-1 si aucun). */
    dsm_safe_list readers; /**< Liste des workers ayant une copie en lecture */

    dsm_drain_queue pending_reqs; /**< Liste des requêtes en attente sur cette page */

    /**
     * @warning : Il n'est utilisé que dans le cadre d'une requête bloquante
     * @warning : Initialisé lors de sa création
     * @warning : Réinitialisé à chaque fin de traitement de requête (pop de la queue)
     */
    pending_reqs_head_ctx
        head_req_ctx; /**< Contexte spécifique à la requête en tête de la queue */

    struct dsm_hlist_node node; /**< Nœud d'ancrage dans la hashmap */
} dsm_page_content;             // Pour pages_reg

typedef struct {

    pthread_mutex_t lock;
    unsigned int missing_barr_reqs;
    unsigned int missing_fina_reqs;
} dsm_mgr_sync;

typedef struct {

    // map des pages : page_id -> dsm_page_content
    dsm_safe_map pages_reg;
    dsm_mgr_sync sync;

} dsm_manager_ctx;

extern dsm_manager_ctx *dsm_mgr_ctx;

int dsm_mgr_ctx_init();
void dsm_mgr_ctx_cleanup();

void dispatch_call_mgr(void *arg);

#endif // DSM_MANAGER_H