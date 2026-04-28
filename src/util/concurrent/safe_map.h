#ifndef DSM_SAFE_MAP_H
#define DSM_SAFE_MAP_H

#include "util/hashmap.h"

#include <pthread.h>

#include <stddef.h>
#include <stdlib.h>

/**
 * @param heads  nodes
 * @param locks  un par bucket
 * @param nbits  exposant : nombre de buckets = 2^nbits
 */
typedef struct {
    struct dsm_hlist_head *heads;
    pthread_rwlock_t *locks;
    unsigned int nbits;
} dsm_safe_map;

/** Nombre de buckets à partir d'un dsm_safe_map */
#define __DSM_SAFE_SIZE(m) (1u << (m)->nbits)

/** Index du bucket pour une clé donnée */
#define __DSM_SAFE_BKT(m, key) dsm_hash_min((key), (m)->nbits)

/** Accès à la tête d'une bucket */
#define __DSM_SAFE_HEAD(m, bkt) (&(m)->heads[(bkt)])

/** Accès au verrou d'une bucket */
#define __DSM_SAFE_LOCK(m, bkt) (&(m)->locks[(bkt)])

static inline int dsm_safe_map_init(dsm_safe_map *m, unsigned int bits) {
    unsigned int i;
    unsigned int sz = 1u << bits;

    m->nbits = bits;

    m->heads = malloc(sz * sizeof(*m->heads));
    if (!m->heads)
        return -1;

    m->locks = malloc(sz * sizeof(*m->locks));
    if (!m->locks) {
        free(m->heads);
        m->heads = NULL;
        return -1;
    }

    for (i = 0; i < sz; i++) {
        DSM_INIT_HLIST_HEAD(&m->heads[i]);
        int rc = pthread_rwlock_init(&m->locks[i], NULL);
        if (rc != 0) {
            while (i--)
                pthread_rwlock_destroy(&m->locks[i]);
            free(m->locks);
            free(m->heads);
            m->heads = NULL;
            m->locks = NULL;
            return rc;
        }
    }
    return 0;
}

/**
 * @warning Ne supprime PAS les éléments de la map : vider la map
 *          (dsm_hash_safe_del sur chaque entrée) avant d'appeler destroy
 */
static inline void dsm_safe_map_destroy(dsm_safe_map *m) {
    if (!m)
        return;
    if (m->locks) {
        unsigned int i;
        for (i = 0; i < __DSM_SAFE_SIZE(m); i++)
            pthread_rwlock_destroy(&m->locks[i]);
        free(m->locks);
        m->locks = NULL;
    }
    if (m->heads) {
        free(m->heads);
        m->heads = NULL;
    }
}

#define dsm_hash_safe_add(m, node, key)                                                  \
    do {                                                                                 \
        unsigned int __bkt = __DSM_SAFE_BKT((m), (key));                                 \
        pthread_rwlock_wrlock(__DSM_SAFE_LOCK((m), __bkt));                              \
        dsm_hlist_add_head((node), __DSM_SAFE_HEAD((m), __bkt));                         \
        pthread_rwlock_unlock(__DSM_SAFE_LOCK((m), __bkt));                              \
    } while (0)

#define dsm_hash_safe_del(m, node, key)                                                  \
    do {                                                                                 \
        unsigned int __bkt = __DSM_SAFE_BKT((m), (key));                                 \
        pthread_rwlock_wrlock(__DSM_SAFE_LOCK((m), __bkt));                              \
        dsm_hlist_del_init((node));                                                      \
        pthread_rwlock_unlock(__DSM_SAFE_LOCK((m), __bkt));                              \
    } while (0)

/**
 * @brief Itère sur les entrées d'une hash map thread-safe correspondant à une clé.
 * 
 * @warning TOUJOURS appeler dsm_hash_safe_get_end après la boucle, même si
 * aucune entrée n'est trouvée
 * @warning Ne pas faire de break ou return qui échappe au end
 *
 * @note Voir dsm_hash_safe_get et dsm_hash_safe_get_end comme 2 parties à coller
 */
#define dsm_hash_safe_get(m, obj, member, key)                                           \
    do {                                                                                 \
        unsigned int __get_bkt = __DSM_SAFE_BKT((m), (key));                             \
        pthread_rwlock_rdlock(__DSM_SAFE_LOCK((m), __get_bkt));                          \
    dsm_hlist_for_each_entry(obj, __DSM_SAFE_HEAD((m), __get_bkt), member)

#define dsm_hash_safe_get_end(m, key)                                                    \
    pthread_rwlock_unlock(__DSM_SAFE_LOCK((m), __DSM_SAFE_BKT((m), (key))));             \
    }                                                                                    \
    while (0)

/**
 * @example
 *   unsigned int bkt;
 *   struct item *obj;
 *   dsm_hash_safe_for_each(&map, bkt, obj, node) {
 *       printf("%d\n", obj->val);
 *   }
 */
#define dsm_hash_safe_for_each(m, bkt, obj, member)                                      \
    for ((bkt) = 0; (bkt) < __DSM_SAFE_SIZE(m); (bkt)++)                                 \
        for (pthread_rwlock_rdlock(__DSM_SAFE_LOCK((m), (bkt))),                         \
             obj = dsm_hlist_entry_safe(__DSM_SAFE_HEAD((m), (bkt))->first,              \
                                        typeof(*(obj)), member);                         \
             (obj) ? 1 : (pthread_rwlock_unlock(__DSM_SAFE_LOCK((m), (bkt))), 0);        \
             obj = dsm_hlist_entry_safe((obj)->member.next, typeof(*(obj)), member))

/**
 * @example : Si id_node n'est pas unique :
 *            dsm_hash_safe_unlock(&spvs_ctx->spvs_reg, id_node);
 *            break;
 *
 * @warning : Si id_node est unique :
 *            on peut ne pas break et laisser la boucle se finir naturellement sur
 *            dsm_hash_safe_get_end
 */
#define dsm_hash_safe_unlock(m, bkt) pthread_rwlock_unlock(__DSM_SAFE_LOCK((m), (bkt)));

/**
 * @warning safe pour la suppression de l'entrée courante uniquement
 *
 * @warning Lors de la suppression, le lock du bucket est toujours tenu :
 *          appeler dsm_hlist_del_init directement (PAS dsm_hash_safe_del
 *          qui tenterait de re-acquérir le même wrlock -> deadlock)
 *
 * @example
 *   unsigned int bkt;
 *   struct dsm_hlist_node *tmp;
 *   struct item *obj;
 *   dsm_hash_safe_for_each_safe(&map, bkt, tmp, obj, node) {
 *       if (obj->val == to_remove)
 *           dsm_hlist_del_init(&obj->node); !! ATTENTION ICI !!
 *   }
 */
#define dsm_hash_safe_for_each_safe(m, bkt, tmp, obj, member)                            \
    for ((bkt) = 0; (bkt) < __DSM_SAFE_SIZE(m); (bkt)++)                                 \
        for (pthread_rwlock_wrlock(__DSM_SAFE_LOCK((m), (bkt))),                         \
             obj = dsm_hlist_entry_safe(__DSM_SAFE_HEAD((m), (bkt))->first,              \
                                        typeof(*(obj)), member);                         \
             (obj) ? ((tmp) = (obj)->member.next, 1)                                     \
                   : (pthread_rwlock_unlock(__DSM_SAFE_LOCK((m), (bkt))), 0);            \
             obj = dsm_hlist_entry_safe((tmp), typeof(*(obj)), member))

/**
 * @return 1 si la map est entièrement vide, 0 sinon
 * @warning peut être périmé dès le retour
 */
static inline int dsm_safe_map_empty(dsm_safe_map *m) {
    unsigned int i;
    for (i = 0; i < __DSM_SAFE_SIZE(m); i++) {
        pthread_rwlock_rdlock(&m->locks[i]);
        int empty = dsm_hlist_empty(&m->heads[i]);
        pthread_rwlock_unlock(&m->locks[i]);
        if (!empty)
            return 0;
    }
    return 1;
}

/**
 * @warning peut être périmé dès le retour, PAS UTILISER pour du "CAS"
 */
static inline size_t dsm_safe_map_count(dsm_safe_map *m) {
    size_t total = 0;
    unsigned int i;
    for (i = 0; i < __DSM_SAFE_SIZE(m); i++) {
        struct dsm_hlist_node *node;
        pthread_rwlock_rdlock(&m->locks[i]);
        for (node = m->heads[i].first; node; node = node->next)
            total++;
        pthread_rwlock_unlock(&m->locks[i]);
    }
    return total;
}

/**
 * @brief insert-if-absent
 * @return 1 si inséré, 0 si la clé existait déjà
 *
 * @warning La vérification et l'insertion sont atomiques
            On évite donc de faire un "contains + add" qui n'est pas atomique
 *
 * @example
 * dsm_supervising_node *obj;
 * dsm_supervising_node *new_node = make_node(id_node);
 *
 * if (!dsm_hash_safe_add_unique(&spvs_ctx->spvs_reg,
 *                               &new_node->hnode,
 *                               obj,
 *                               hnode,
 *                               id,
 *                               id_node
 *                               )) {
 *     free(new_node); // doublon
 * }
 *
 */
#define dsm_hash_safe_add_unique(m, node, obj, member, key_field, key)                   \
    ({                                                                                   \
        unsigned int __bkt = __DSM_SAFE_BKT((m), (key));                                 \
        int __inserted = 0;                                                              \
        pthread_rwlock_wrlock(__DSM_SAFE_LOCK((m), __bkt));                              \
        int __found = 0;                                                                 \
        dsm_hlist_for_each_entry(obj, __DSM_SAFE_HEAD((m), __bkt), member) {             \
            if ((obj)->key_field == (key)) {                                             \
                __found = 1;                                                             \
                break;                                                                   \
            }                                                                            \
        }                                                                                \
        if (!__found) {                                                                  \
            dsm_hlist_add_head((node), __DSM_SAFE_HEAD((m), __bkt));                     \
            __inserted = 1;                                                              \
        }                                                                                \
        pthread_rwlock_unlock(__DSM_SAFE_LOCK((m), __bkt));                              \
        __inserted;                                                                      \
    })

#endif /* DSM_SAFE_MAP_H */