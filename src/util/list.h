#ifndef DSM_LIST_H
#define DSM_LIST_H

#define dsm_container_of(ptr, type, member)                                              \
    ((type *)((char *)(ptr) - offsetof(type, member)))

typedef struct dsm_list_node {
    struct dsm_list_node *next;
    struct dsm_list_node *prev;
} dsm_list_node_t;

#endif // DSM_LIST_H