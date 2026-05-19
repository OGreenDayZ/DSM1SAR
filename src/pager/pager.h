#ifndef DSM_PAGER_H
#define DSM_PAGER_H

#include <asm-generic/int-ll64.h>
#include <linux/userfaultfd.h>
#include <stdint.h>

/**
 * La taille de la page est différente d'un système à un autre
 */
#define DSM_DEFAULT_PAGESIZE sysconf(_SC_PAGESIZE)
#define DSM_DEFAULT_NB_PAGES 16384

/**
 * @brief Type de requête émise par un worker vers le manager.
 *
 * Indique l'intention du worker lorsqu'il demande l'accès à une page.
 */
typedef enum {
    UNLOCK_REQ, /**< Libération du verrou exclusif. */
    READ_REQ,   /**< Demande d'accès en lecture. */
    WRITE_REQ,  /**< Demande d'accès en écriture (implique l'invalidation
                 *   de la page chez les autres workers). */
    LOCK_REQ,   /**< Demande de verrouillage exclusif. */
} page_mode_req;

typedef enum {
    EMPTY_INIT,
    NOT_EMPTY,
    PROMOTE_ONLY
} data_info;

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
 * @return -1 en cas d'erreur.
 */
int userfaultfd_init(int *out_uffd, uint64_t *out_range_start);
/**
 * Active ou désactive la protection en écriture sur une page déjà présente en mémoire.
 *
 * @param page_id  page_id de la page à protéger en écriture.
 * @param protection 0 = désactiver la protection, UFFDIO_WRITEPROTECT_MODE_WP = active la
 * protection.
 *
 * @return 0 en cas de succès.
 * @return -1 en cas d'erreur.
 */
int pager_protect(int uffd, int page_id, int protection);

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
 *              UFFDIO_COPY_MODE_WP = copie avec protection en écriture immédiate (la page
 * est en lecture seule après la copie).
 *
 * @return 0 en cas de succès.
 * @return -1 en cas d'erreur.
 */
int pager_copy(int uffd, int page_id, __u64 src, int mode);

/**
 * Libère toute les ressources alloué par le pager.
 *
 * @return 0 en cas de succés.
 * @return -1 en cas d'erreur.
 */
int pager_destroy();

#endif // DSM_PAGER_H