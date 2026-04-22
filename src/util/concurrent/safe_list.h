#ifndef DSM_SAFE_LIST_H
#define DSM_SAFE_LIST_H

#include "util/list.h"

#include <pthread.h>
#include <stddef.h>

typedef struct {
    dsm_list_head head;
    pthread_rwlock_t lock;
} dsm_safe_list;

int dsm_safe_list_init(dsm_safe_list *list);
void dsm_safe_list_destroy(dsm_safe_list *list);
void dsm_safe_list_add(dsm_safe_list *list, dsm_list_head *new);
void dsm_safe_list_add_tail(dsm_safe_list *list, dsm_list_head *new);
void dsm_safe_list_del(dsm_safe_list *list, dsm_list_head *entry);
int dsm_safe_list_empty(dsm_safe_list *list);
size_t dsm_safe_list_count(dsm_safe_list *list);

#define dsm_safe_list_for_each_entry(list, pos, member)                                  \
    for (pthread_rwlock_rdlock(&(list)->lock),                                           \
         pos = dsm_container_of((list)->head.next, typeof(*(pos)), member);              \
         &(pos)->member != &(list)->head ? 1                                             \
                                         : (pthread_rwlock_unlock(&(list)->lock), 0);    \
         pos = dsm_container_of((pos)->member.next, typeof(*(pos)), member))

/**
 * @warning : toujours supprimer par dsm_list_del et pas dsm_safe_list_del
 */
#define dsm_safe_list_for_each_entry_safe(list, pos, n, member)                          \
    for (pthread_rwlock_wrlock(&(list)->lock),                                           \
         pos = dsm_container_of((list)->head.next, typeof(*(pos)), member),              \
         n = dsm_container_of((pos)->member.next, typeof(*(pos)), member);               \
         &(pos)->member != &(list)->head ? 1                                             \
                                         : (pthread_rwlock_unlock(&(list)->lock), 0);    \
         pos = n, n = dsm_container_of((n)->member.next, typeof(*(n)), member))

/**
 * @note : pour les sorties avant fin de boucle
 */
void dsm_safe_list_unlock(dsm_safe_list *list);

#endif /* DSM_SAFE_LIST_H */