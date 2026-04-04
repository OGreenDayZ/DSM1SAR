#include "list.h"

void dsm_list_add(dsm_list_head_t *new, dsm_list_head_t *head) {
    dsm_list_head_t *prev = head;
    dsm_list_head_t *next = head->next;

    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

void dsm_list_add_tail(dsm_list_head_t *new, dsm_list_head_t *head) {
    dsm_list_head_t *prev = head->prev;
    dsm_list_head_t *next = head;

    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

void dsm_list_del(dsm_list_head_t *entry) {
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    entry->next = NULL;
    entry->prev = NULL;
}

int dsm_list_empty(const dsm_list_head_t *head) {
    return head->next == head;
}

size_t dsm_list_count(const dsm_list_head_t *head) {
    size_t count = 0;
    const dsm_list_head_t *current = head->next;

    while (current != head) {
        count++;
        current = current->next;
    }

    return count;
}