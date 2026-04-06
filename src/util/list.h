#ifndef DSM_LIST_H
#define DSM_LIST_H

#include "container_of.h"
#include <stddef.h>

typedef struct dsm_list_head {
    struct dsm_list_head *next, *prev;
} dsm_list_head;

#define DSM_LIST_HEAD_INIT(name) {&(name), &(name)}

static inline void DSM_LIST_INIT(dsm_list_head *list) {
    list->next = list;
    list->prev = list;
}

void dsm_list_add(dsm_list_head *new, dsm_list_head *head);
void dsm_list_add_tail(dsm_list_head *new, dsm_list_head *head);
void dsm_list_del(dsm_list_head *entry);
int dsm_list_empty(const dsm_list_head *head);
size_t dsm_list_count(const dsm_list_head *head);

#define dsm_list_for_each_entry(pos, head, member)                                       \
    for (pos = dsm_container_of((head)->next, typeof(*pos), member);                     \
         &pos->member != (head);                                                         \
         pos = dsm_container_of(pos->member.next, typeof(*pos), member))

#define dsm_list_for_each_entry_safe(pos, n, head, member)                               \
    for (pos = dsm_container_of((head)->next, typeof(*pos), member),                     \
        n = dsm_container_of(pos->member.next, typeof(*pos), member);                    \
         &pos->member != (head);                                                         \
         pos = n, n = dsm_container_of(n->member.next, typeof(*n), member))

#endif // DSM_LIST_H