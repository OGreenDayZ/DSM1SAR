#ifndef DSM_INTERNAL_H
#define DSM_INTERNAL_H

#include "dsm.h"

typedef struct {
    /* Une fois le discover fini, config devient READ-ONLY */
    dsm_config config;
    unsigned int node_id;
    // registry_nodes
    // registry_pages

    // à étendre...
} dsm_context;

extern dsm_context *dsm_ctx;

#endif // DSM_INTERNAL_H
