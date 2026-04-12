#include "thread_pool.h"
#include <debug/debug.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

dsm_thread_pool *pool_create(int nb_threads, int size_q) {
    dsm_thread_pool *pool = malloc(sizeof(dsm_thread_pool));
    int i, res;
    if (!pool) {
        perror("malloc pool");
        goto error;
    }
    pool->thread_count = nb_threads;
    pool->shutdown = 0;
    pool->threads = malloc(sizeof(pthread_t) * nb_threads);
    pool->thread_started = 0;
    if (!pool->threads) {
        perror("malloc threads");
        goto error;
    }

    // Init de la queue
    pool->queue = malloc(sizeof(dsm_task_queue));
    if (!pool->queue) {
        perror("malloc queue");
        goto error;
    }

    pool->queue->capacity = size_q;
    pool->queue->count = 0;
    pool->queue->head = 0;
    pool->queue->tail = 0;

    if (pthread_mutex_init(&(pool->queue->mutex), NULL)) {
        perror("init mutex");
        goto error;
    }

    if (pthread_cond_init(&pool->queue->notify, NULL)) {
        perror("init cond variable");
        goto error;
    }

    pool->queue->tasks = malloc(sizeof(dsm_thread_task) * size_q);
    if (!pool->queue->tasks) {
        perror("malloc tasks");
        goto error;
    }
    // Initialiser les threads :

    for (i = 0; i < nb_threads; i++) {
        res = pthread_create(&pool->threads[i], NULL, pool_slave_routine, (void *)pool);
        if (res) {
            perror("pthread create");
            pool_destroy(pool, graceful_shutdown);
            return NULL;
        }
        pool->thread_started++;
    }
    return pool;

error:
    if (pool)
        pool_free(pool); // le free de la queue si elle est alloué se passe dedans
    return NULL;
}

void *pool_slave_routine(void *threadpool) {
    dsm_thread_pool *pool = (dsm_thread_pool *)threadpool;
    dsm_task_queue *queue = pool->queue;
    dsm_thread_task task;
    while (1) {
        pthread_mutex_lock(&pool->queue->mutex);

        while ((queue->count == 0) && (!pool->shutdown)) {
            pthread_cond_wait(&queue->notify, &queue->mutex);
        }

        // ! si on a un graceful shutdown on laisse les threads prends des tâche jusqu'a
        // la fin
        if ((pool->shutdown == brutal_shutdown) ||
            (pool->shutdown == graceful_shutdown && queue->count == 0))
            break;

        task.func = queue->tasks[queue->head].func;
        task.args = queue->tasks[queue->head].args;
        queue->count--;
        queue->head = (queue->head + 1) % queue->capacity; // liste circulaire
        pthread_mutex_unlock(&pool->queue->mutex);
        (*(task.func))(task.args);
    }
    pool->thread_started--; // psq arrivé ici le thread à fini son job
    pthread_mutex_unlock(&pool->queue->mutex);
    pthread_exit(NULL);
    return NULL;
}

// ajoute une tache dans la queue
int pool_add_task(dsm_thread_pool *pool, void (*func)(void *), void *arg) {
    if (pool == NULL || func == NULL) {
        perror("invalid arguments add_task");
        return THREAD_INVALID_ARG;
    }
    dsm_task_queue *queue = pool->queue;
    int next;

    if (pthread_mutex_lock(&queue->mutex) != 0)
        goto error_mutex;

    // Peu importe si c'est graceful ou pas, on arrête de prendre de nouvelles tâches
    if (pool->shutdown) {
        if (pthread_mutex_unlock(&queue->mutex) != 0)
            goto error_mutex;
        return THREAD_SHUTING_DOWN;
    }

    // Queue rempli, les consommateurs doivent consommer
    if (queue->count == queue->capacity) {
        if (pthread_mutex_unlock(&queue->mutex) != 0)
            goto error_mutex;
        return THREAD_FULL_QUEUE;
    }

    // Nouvelle fin de liste apres un ajout (vu qu'on est dans une liste circulaire ->
    // module la taille de la liste)
    next = (queue->tail + 1) % queue->capacity;
    queue->tasks[queue->tail].func = func;
    queue->tasks[queue->tail].args = arg;
    queue->tail = next;
    queue->count++; // on ajouter une tâche

    if (pthread_cond_broadcast(&queue->notify) != 0) {
        if (pthread_mutex_unlock(&queue->mutex) != 0)
            goto error_mutex;
        perror("cond_broadcast add_task");
        return THREAD_COND_ERR;
    }

    if (pthread_mutex_unlock(&queue->mutex) != 0)
        goto error_mutex;
    return 0;
error_mutex:
    perror("mutex add_task");
    return THREAD_MUTEX_ERR;
}

int pool_destroy(dsm_thread_pool *pool, dsm_pool_shutdown shutdown) {
    dsm_task_queue *queue = pool->queue;
    int i, to_join;
    if (pthread_mutex_lock(&queue->mutex)) {
        perror("lock destroy");
        return THREAD_MUTEX_ERR;
    }
    pool->shutdown = shutdown;
    // Reveil tout les threads endormis
    if (pthread_cond_broadcast(&queue->notify) != 0) {
        if (pthread_mutex_unlock(&queue->mutex) != 0) {
            perror("unlock destroy");
            return THREAD_MUTEX_ERR;
        }
        perror("cond broadcast empty");
        return THREAD_COND_ERR;
    }

    to_join = pool->thread_started;
    if (pthread_mutex_unlock(&queue->mutex) != 0) {
        perror("unlock destroy");
        return THREAD_MUTEX_ERR;
    }

    for (i = 0; i < to_join; i++) {
        if (pthread_join(pool->threads[i], NULL) != 0) {
            perror("join destroy");
            return THREAD_JOIN_ERR;
        }
    }

    pool_free(pool);
    return 0;
}

int pool_free(dsm_thread_pool *pool) {
    if (pool == NULL) {
        perror("invalid argument free");
        return THREAD_INVALID_ARG;
    }

    if (pool->thread_started > 0) {
        perror("cannot free because some threads are still active");
        return THREAD_ERR;
    }

    if (pool->threads) {
        free(pool->threads);
    }
    if (pool->queue) {
        if (pool->queue->tasks) {
            free(pool->queue->tasks);
        }
        pthread_mutex_destroy(&(pool->queue->mutex));
        pthread_cond_destroy(&(pool->queue->notify));
        free(pool->queue);
    }

    free(pool);
    return 0;
}
