#ifndef DSM_INTERNAL_H
#define DSM_INTERNAL_H

#include "dsm.h"

typedef struct {
    dsm_config_t config;
} dsm_context_t;

extern dsm_context_t *dsm_ctx;

#endif // DSM_INTERNAL_H
