#ifndef TEST_FUNCTIONS_H
#define TEST_FUNCTIONS_H

#include <pthread.h>
#include "dsm.h"

typedef struct {
    void *addr;
    int id;
    int ret; 
} thread_test_arg_t;

typedef struct {
    int thread_id;
    int *shared_mem;
} barrier_test_arg_t;

void *test_simple_read(void *arg);
void *test_simple_write(void *arg);

void *test_concurrent_writes(void *arg);
void *test_lock_unlock(void *arg);
void *test_concurrent_writes_with_locks(void *arg);
void *test_concurrent_reads_with_locks(void *arg);

void *test_barrier_thread(void *arg);
void *test_lock_unlock_barrier_thread(void *arg);
void *test_barrier_advanced_thread(void *arg);
int test_init(dsm_config *config, int *node_id);

void *test_coherence_read_write(void *arg); 
void *test_concurrent_write_with_locks(void *arg); 
#endif // TEST_FUNCTIONS_H