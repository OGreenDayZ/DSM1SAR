#include "dsm.h"
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

// Structure pour passer les arguments aux threads de test
typedef struct {
    void *addr;
    int thread_id;
} test_arg_t;

// Scénario 1 : Test de concurrence READ/READ (Mécanisme Passager/Messager)
void *thread_read_concurrency(void *arg) {
    test_arg_t *t = (test_arg_t *)arg;
    printf("[Thread %lu] Tentative de lecture à l'adresse %p\n",
           (unsigned long)pthread_self(), t->addr);

    // Accès en lecture : déclenche un pagefault UFFD_PAGEFAULT_FLAG_WRITE non positionné
    int val = *(int *)(t->addr);

    printf("[Thread %lu] Lecture réussie, valeur : %d\n", (unsigned long)pthread_self(),
           val);
    return NULL;
}

// Scénario 2 : Test de montée en priorité (READ puis WRITE)
void *thread_write_upgrade(void *arg) {
    test_arg_t *t = (test_arg_t *)arg;
    printf("[Thread %lu] Tentative d'écriture à l'adresse %p\n",
           (unsigned long)pthread_self(), t->addr);

    // Accès en écriture : déclenche un pagefault avec flag WRITE
    *(int *)(t->addr) = 42 + (unsigned long)pthread_self();

    printf("[Thread %lu] Écriture réussie\n", (unsigned long)pthread_self());
    return NULL;
}

// Scénario 3 : Test des Verrous (LOCK/UNLOCK)
void *thread_lock_test(void *arg) {
    test_arg_t *t = (test_arg_t *)arg;

    printf("[Thread %lu] Demande de LOCK sur %p\n", (unsigned long)pthread_self(),
           t->addr);
    if (dsm_lock(t->addr) == 0) {
        printf("[Thread %lu] LOCK acquis, section critique...\n",
               (unsigned long)pthread_self());
        sleep(1); // Simule un travail sous verrou
        dsm_unlock(t->addr);
        printf("[Thread %lu] UNLOCK effectué\n", (unsigned long)pthread_self());
    }

    return NULL;
}

int main() {
    dsm_config config;
    dsm_get_default_config(&config);

    // Configuration minimale pour le test
    config.net.system.nb_nodes = 2;
    config.net.system.nb_managers = 1;
    config.net.node.role = DSM_ROLE_WORKER;

    if (dsm_init(&config) < 0) {
        fprintf(stderr, "Échec d'initialisation DSM\n");
        return -1;
    }

    // Allocation d'une page
    void *page_ptr = (void *)dsm_alloc();
    if (!page_ptr)
        return -1;

    pthread_t threads[5];
    test_arg_t args[5];

    printf("\n--- CAS 1 & 2 : Concurrence d'accès (Messager/Passager/Priorité) ---\n");
    for (int i = 0; i < 3; i++) {
        args[i].addr = page_ptr;
        args[i].thread_id = i;
        if (i == 2) {
            // Le dernier thread tente une écriture pour tester l'annulation du READ
            // non
            // commencé
            pthread_create(&threads[i], NULL, thread_write_upgrade, &args[i]);
        } else {
            pthread_create(&threads[i], NULL, thread_read_concurrency, &args[i]);
        }
    }

    for (int i = 0; i < 3; i++)
        pthread_join(threads[i], NULL);

    printf("\n--- CAS 3 : Test des verrous exclusifs (LOCK_REQ) ---\n");
    for (int i = 3; i < 5; i++) {
        args[i].addr = page_ptr;
        args[i].thread_id = i;
        pthread_create(&threads[i], NULL, thread_lock_test, &args[i]);
    }

    for (int i = 3; i < 5; i++)
        pthread_join(threads[i], NULL);

    dsm_finalize();
    return 0;
}