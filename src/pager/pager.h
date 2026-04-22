#ifndef DSM_PAGER_H
#define DSM_PAGER_H

#include <unistd.h>
#include "util/hashmap.h"
#include "util/list.h"
#include <linux/userfaultfd.h>
#include "internal.h"
/**
 * La taille de la page est différente d'un système à un autre
 */
#define DSM_DEFAULT_PAGESIZE sysconf(_SC_PAGESIZE)
#define DSM_DEFAULT_NB_PAGES 16384

/** Taille effective d'une page (en octets), lue depuis le contexte DSM. */
#define EFFECTIVE_PAGESIZE dsm_ctx->config.pager.nb_pages

/** Nombre effectif de pages, lu depuis le contexte DSM. */
#define EFFECTIVE_NB_PAGES dsm_ctx->config.pager.page_size

/** Hashmap des pages gérées par ce manager (@ref dsm_page). */
#define MAP_MANAGER dsm_ctx->node_ctx.manager_ctx.pages_reg

/** Map worker : associe un manager_id à un node_id. */
#define MAP_WORKER dsm_ctx->node_ctx.worker_ctx.managerID_to_nodeID

/** Adresse de début de la région mémoire surveillée par userfaultfd. */
#define RANGE_START dsm_ctx->node_ctx.worker_ctx.range_start
/**
 * Descripteur userfaultfd du worker courant.
 * Initialisé par @ref userfaultfd_init, utilisé dans toute la boucle worker.
 */
extern int uffd;

/**
 * @brief Mode de détention d'une page par son propriétaire courant.
 *
 * Décrit les droits avec lesquels le propriétaire détient la page.
 * Le manager met à jour ce champ à chaque transfert de propriété.
 */
typedef enum{
    PAGE_R, 
    PAGE_RW, 
    PAGE_LOCKED
} dsm_page_mode_poss; 

/**
 * @brief Type de requête émise par un worker vers le manager.
 *
 * Indique l'intention du worker lorsqu'il demande l'accès à une page.
 */
typedef enum {
    READ_REQ,  /**< Demande d'accès en lecture. */
    WRITE_REQ, /**< Demande d'accès en écriture (implique l'invalidation
                 *   de la page chez les autres workers). */
    LOCK_REQ,  /**< Demande de verrouillage exclusif. */
    UNLOCK_REQ /**< Libération du verrou exclusif. */
} page_mode_req; 

/**
 * @brief Entrée de la table des pages gérées par un manager.
 *
 * Une instance de @c dsm_page est créée par le manager lors du premier
 * accès en écriture à une page. Elle est stockée dans @ref MAP_MANAGER
 * (hashmap indexée par @c id) et conservée jusqu'à la fin de la session.
 */
typedef struct {
    unsigned int id;              /**< Identifiant de la page (clé de hashmap). */
    dsm_page_mode_poss mode;      /**< Mode de possession par le propriétaire actuel. */
    int owner_id;                 /**< ID du nœud propriétaire (-1 si aucun). */
    struct dsm_list_head workers; /**< Liste des workers ayant une copie en lecture
                                  *   (@ref worker_info). */
    struct dsm_hlist_node node;   /**< Nœud d'ancrage dans la hashmap @ref MAP_MANAGER. */
} dsm_page;


/**
 * Entrée de la liste des workers détenant une copie d'une page.
 *
 * Chaînée via `dsm_page.workers`. 
 *
 * @param worker_id  Identifiant du worker concerné.
 * @param node       Nœud d'ancrage dans la liste `dsm_page.workers`.
 */
typedef struct {
    int worker_id; 
    struct dsm_list_head node; 
} worker_info; // liste des worker qui ont la page (pour la page dsm_page)

/**
 * Initialise le descripteur userfaultfd pour le worker courant.
 *
 * Crée un fd userfaultfd avec les flags O_CLOEXEC | O_NONBLOCK,
 * vérifie la compatibilité de l'API noyau et enregistre la plage
 * mémoire [RANGE_START, RANGE_START + EFFECTIVE_NB_PAGES * EFFECTIVE_PAGESIZE[
 * pour la surveillance des fautes de page (UFFDIO_REGISTER_MODE_MISSING |
 * UFFDIO_REGISTER_MODE_WP).
 *
 * @return Le descripteur userfaultfd (>= 0) en cas de succès.
 * @return -1 en cas d'erreur, errno positionné.
 */
int userfaultfd_init() ; 
/**
 * Active la protection en écriture sur une page déjà présente en mémoire.
 *
 * Utilisé par le worker pour rétrograder une page de RW à R. Toute écriture
 * ultérieure sur cette page déclenchera une faute UFFD_PAGEFAULT_FLAG_WP.
 *
 * @param page_id  page_id de la page à protéger en écriture.
 *
 * @return 0 en cas de succès.
 * @return -1 en cas d'erreur, errno positionné.
 */
int pager_writeprotect(int page_id); 

/**
 * Copie une page dans la plage surveillée par userfaultfd.
 *
 * Utilisé par le worker pour résoudre une faute de page après réception
 * des données depuis le manager. L'opération utilise UFFDIO_COPY pour
 * injecter les données directement dans la page fautive.
 *
 * @param fd    Descripteur userfaultfd du worker.
 * @param src   Adresse source contenant les données à copier
 *              (taille EFFECTIVE_PAGESIZE).
 * @param mode  0 = copie sans protection en écriture (la page est accessible
 *              en RW après la copie) ;
 *              UFFDIO_COPY_MODE_WP = copie avec protection en écriture immédiate (la page est
 *              en lecture seule après la copie).
 *
 * @return 0 en cas de succès.
 * @return -1 en cas d'erreur, errno positionné.
 */
int pager_copy(int page_id, __u64 src, int mode); 

// TODO majda
int pager_destroy();


#endif // DSM_PAGER_H