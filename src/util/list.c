#include "list.h"

void dsm_list_add(dsm_list_head *new, dsm_list_head *head) {
    dsm_list_head *prev = head;
    dsm_list_head *next = head->next;

    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

void dsm_list_add_tail(dsm_list_head *new, dsm_list_head *head) {
    dsm_list_head *prev = head->prev;
    dsm_list_head *next = head;

    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

void dsm_list_del(dsm_list_head *entry) {
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    entry->next = NULL;
    entry->prev = NULL;
}

int dsm_list_empty(const dsm_list_head *head) {
    return head->next == head;
}

size_t dsm_list_count(const dsm_list_head *head) {
    size_t count = 0;
    const dsm_list_head *current = head->next;

    while (current != head) {
        count++;
        current = current->next;
    }

    return count;
}