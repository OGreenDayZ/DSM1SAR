#ifndef DSM_DRAIN_QUEUE_H
#define DSM_DRAIN_QUEUE_H

#include <pthread.h>
#include <stddef.h>

/**
 * @brief Queue concurrente avec transfert de propriété
 *        - MPSC : plusieurs producteurs, un seul consommateur (drainer)
 *
 * @details :
 *   - dsm_dq_push() retourne 1 si l'élément est en tête de queue
 *     -> le thread appelant doit devenir drainer.
 *   - dsm_dq_pop() retourne 1 si la queue est maintenant vide
 *     -> le drainer doit retourner dans son pool.
 *
 */

/* Node */
typedef struct dsm_dq_node {
    struct dsm_dq_node *next;
} dsm_dq_node;

/* Queue */
typedef struct {
    dsm_dq_node *head;
    dsm_dq_node *tail;
    pthread_mutex_t lock;
} dsm_drain_queue;

static inline int dsm_dq_init(dsm_drain_queue *q) {
    q->head = NULL;
    q->tail = NULL;
    return pthread_mutex_init(&q->lock, NULL);
}

/**
 * @warning Détruit la queue MAIS ne libère pas les nœuds
 */
static inline void dsm_dq_destroy(dsm_drain_queue *q) {
    if (q) {
        pthread_mutex_destroy(&q->lock);
    }
}

/**
 * @return 1 si le nœud est en tête (-> drainer)
 *         0 sinon (-> tâche confiée)
 */
static inline int dsm_dq_push(dsm_drain_queue *q, dsm_dq_node *node) {
    int is_first;

    node->next = NULL;

    pthread_mutex_lock(&q->lock);
    if (q->tail) {
        q->tail->next = node;

    } else {
        q->head = node;
    }

    q->tail = node;
    is_first = (q->head == node);
    pthread_mutex_unlock(&q->lock);

    return is_first;
}

/**
 * @brief Retourne le nœud en tête sans le retirer
 * @return Pointeur vers le nœud en tête, ou NULL si la queue est vide
 * @warning Doit être appelé uniquement par le drainer !
 */
static inline dsm_dq_node *dsm_dq_front(dsm_drain_queue *q) {
    dsm_dq_node *node;
    pthread_mutex_lock(&q->lock);
    node = q->head;
    pthread_mutex_unlock(&q->lock);
    return node;
}

/**
 * @brief Retire le nœud en tête
 * @return 1 si la queue est maintenant vide (drainer -> retour au pool),
 *         0 sinon (drainer continue)
 * @warning Doit être appelé uniquement par le drainer, après dsm_dq_front()
 */
static inline int dsm_dq_pop(dsm_drain_queue *q) {
    int is_empty;

    pthread_mutex_lock(&q->lock);
    q->head = q->head->next;
    if (!q->head) {
        q->tail = NULL;
    }

    is_empty = (q->head == NULL);
    pthread_mutex_unlock(&q->lock);

    return is_empty;
}

#endif /* DSM_DRAIN_QUEUE_H */