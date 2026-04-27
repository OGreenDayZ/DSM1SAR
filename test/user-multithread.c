#include "dsm.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NB_THREADS_PER_NODE 3

// Structure pour passer les données aux threads
typedef struct {
    int thread_id;
    int node_id;
    int *shared_data;
} thread_arg_t;

// Mutex et Condition pour la synchro LOCALE (inter-thread)
pthread_mutex_t local_mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t local_cond = PTHREAD_COND_INITIALIZER;
int ready_to_read = 0;

void *thread_work(void *arg) {
    thread_arg_t *t_arg = (thread_arg_t *)arg;
    int tid = t_arg->thread_id;
    int nid = t_arg->node_id;
    int *data = t_arg->shared_data;

    if (nid == 1) {
        // Thread 0 du Nœud 1 écrit, les autres threads du Nœud 1 attendent ou lisent
        if (tid == 0) {
            printf("[Nœud %d - Thread %d] J'écris dans la DSM...\n", nid, tid);
            for (int i = 0; i < 10; i++) {
                data[i] =
                    i * 100; // Déclenche potentiellement une faute de page (écriture)
            }

            pthread_mutex_lock(&local_mtx);
            ready_to_read = 1;
            pthread_cond_broadcast(&local_cond); // Réveille les threads locaux
            pthread_mutex_unlock(&local_mtx);
        } else {
            pthread_mutex_lock(&local_mtx);
            while (!ready_to_read)
                pthread_cond_wait(&local_cond, &local_mtx);
            printf("[Nœud %d - Thread %d] Lecture locale après écriture : data[0] = %d\n",
                   nid, tid, data[0]);
            pthread_mutex_unlock(&local_mtx);
        }
    } else {
        // Autres nœuds : Tentative de lecture concurrente
        // Cela va forcer le daemon DSM à gérer plusieurs requêtes de pages pour un même
        // processus
        printf("[Nœud %d - Thread %d] Tentative de lecture distante...\n", nid, tid);
        printf("[Nœud %d - Thread %d] Valeur lue : %d\n", nid, tid, data[tid]);
    }

    return NULL;
}

int main() {
    dsm_config config;
    dsm_get_default_config(&config);
    config.net.system.nb_nodes = 3;
    config.net.system.nb_managers = 1;

    if (dsm_init(&config) != DSM_SUCCESS) {
        fprintf(stderr, "Erreur d'initialisation DSM\n");
        return EXIT_FAILURE;
    }

    int my_id = dsm_get_node_id();
    int *shared_memory = NULL;

    if (dsm_get_role() == DSM_ROLE_WORKER) {
        shared_memory = (int *)dsm_alloc(sizeof(int) * 100);

        pthread_t threads[NB_THREADS_PER_NODE];
        thread_arg_t args[NB_THREADS_PER_NODE];

        for (int i = 0; i < NB_THREADS_PER_NODE; i++) {
            args[i].thread_id = i;
            args[i].node_id = my_id;
            args[i].shared_data = shared_memory;
            pthread_create(&threads[i], NULL, thread_work, &args[i]);
        }

        for (int i = 0; i < NB_THREADS_PER_NODE; i++) {
            pthread_join(threads[i], NULL);
        }
    } else {
        printf("[Manager] En attente des requêtes de pages...\n");
        sleep(10); // Laisse le temps aux workers
    }

    printf("[Nœud %d] Travail terminé, mise en veille avant fermeture.\n", my_id);
    sleep(5);
    return EXIT_SUCCESS;
}