#include "safe_list.h"

#include <pthread.h>
#include <stddef.h>

#include "util/list.h"

void dsm_safe_list_lock(dsm_safe_list *list) {
    pthread_rwlock_wrlock(&list->lock);
}

int dsm_safe_list_init(dsm_safe_list *list) {
    dsm_list_init(&list->head);
    return pthread_rwlock_init(&list->lock, NULL);
}

void dsm_safe_list_destroy(dsm_safe_list *list) {
    if (!list)
        return;
    pthread_rwlock_destroy(&list->lock);
}

void dsm_safe_list_add(dsm_safe_list *list, dsm_list_head *new) {
    pthread_rwlock_wrlock(&list->lock);
    dsm_list_add(&list->head, new);
    pthread_rwlock_unlock(&list->lock);
}

void dsm_safe_list_add_tail(dsm_safe_list *list, dsm_list_head *new) {
    pthread_rwlock_wrlock(&list->lock);
    dsm_list_add_tail(&list->head, new);
    pthread_rwlock_unlock(&list->lock);
}

void dsm_safe_list_del(dsm_safe_list *list, dsm_list_head *entry) {
    pthread_rwlock_wrlock(&list->lock);
    dsm_list_del(entry);
    pthread_rwlock_unlock(&list->lock);
}

int dsm_safe_list_empty(dsm_safe_list *list) {
    pthread_rwlock_rdlock(&list->lock);
    int empty = dsm_list_empty(&list->head);
    pthread_rwlock_unlock(&list->lock);
    return empty;
}

size_t dsm_safe_list_count(dsm_safe_list *list) {
    pthread_rwlock_rdlock(&list->lock);
    size_t n = dsm_list_count(&list->head);
    pthread_rwlock_unlock(&list->lock);
    return n;
}

void dsm_safe_list_unlock(dsm_safe_list *list) {
    pthread_rwlock_unlock(&list->lock);
}