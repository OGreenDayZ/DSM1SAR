#ifndef DSM_SUPERVISOR_H
#define DSM_SUPERVISOR_H

#include "network/discover/net_discover.h"
#include <stdatomic.h>

typedef struct {
    // mutex id_count&reg
    atomic_int id_count;
    // JOIN LIST ?
    // reg temp
    // pool threads

} dsm_supervisor_ctx;

int dsm_supervisor_loop(dsm_dscv_ctx *dscv_ctx);

#endif // DSM_SUPERVISOR_H
