#ifndef DSM_SUPERVISOR_H
#define DSM_SUPERVISOR_H

#include <stdatomic.h>
#include <stdbool.h>

typedef struct {
    // mutex id_count&reg
    atomic_int id_count;
    // JOIN LIST ?
    // reg temp
    // pool threads

} dsm_supervisor_ctx;

int dsm_supervisor_loop(void);

#endif // DSM_SUPERVISOR_H
