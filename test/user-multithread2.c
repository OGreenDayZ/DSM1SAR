#include "dsm.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// --- Codes de couleurs ANSI ---
#define RESET "\x1b[0m"
#define BOLD "\033[1m"
#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN "\x1b[36m"
#define WHITE "\x1b[37m"

// Structure pour passer les arguments aux threads de test
typedef struct {
    void *addr;
    int thread_id;
} test_arg_t;

// Scénario 1 : Test de concurrence READ/READ
void *thread_read_concurrency(void *arg) {
    test_arg_t *t = (test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();

    printf(BLUE "[Thread %lu] " RESET "Tentative de " BOLD "LECTURE" RESET
                " à l'adresse %p\n",
           tid, t->addr);

    // Accès en lecture : déclenche un pagefault
    int val = *(int *)(t->addr);

    printf(GREEN "[Thread %lu] " RESET "Lecture réussie, valeur : " BOLD "%d" RESET "\n",
           tid, val);
    return NULL;
}

// Scénario 2 : Test de montée en priorité (WRITE)
void *thread_write_upgrade(void *arg) {
    test_arg_t *t = (test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();

    printf(RED "[Thread %lu] " RESET "Tentative d'" BOLD "ÉCRITURE" RESET
               " à l'adresse %p\n",
           tid, t->addr);

    // Accès en écriture
    *(int *)(t->addr) = 42 + (int)tid;

    printf(BOLD GREEN "[Thread %lu] Écriture réussie" RESET "\n", tid);
    return NULL;
}

// Scénario 3 : Test des Verrous (LOCK/UNLOCK)
void *thread_lock_test(void *arg) {
    test_arg_t *t = (test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();

    printf(MAGENTA "[Thread %lu] " RESET "Demande de " BOLD "LOCK" RESET " sur %p\n", tid,
           t->addr);

    if (dsm_lock(t->addr) == 0) {
        printf(BOLD MAGENTA "[Thread %lu] LOCK acquis" RESET ", section critique...\n",
               tid);
        sleep(1); // Simule un travail sous verrou
        dsm_unlock(t->addr);
        printf(CYAN "[Thread %lu] UNLOCK effectué" RESET "\n", tid);
    }

    return NULL;
}

int main() {
    dsm_config config;
    dsm_get_default_config(&config);

    printf(BOLD WHITE "\n[SYSTÈME] Initialisation du DSM..." RESET "\n");
    config.net.system.nb_nodes = 4;
    config.net.system.nb_managers = 1;
    config.net.node.role = DSM_ROLE_WORKER;

    if (dsm_init(&config) < 0) {
        fprintf(stderr, RED "Échec d'initialisation DSM" RESET "\n");
        return -1;
    }

    void *page_ptr = (void *)dsm_alloc();
    if (!page_ptr) {
        fprintf(stderr, RED "Erreur d'allocation dsm_alloc" RESET "\n");
        return -1;
    }
    printf(BOLD CYAN "[SYSTÈME] Page allouée à : %p" RESET "\n", page_ptr);

    pthread_t threads[5];
    test_arg_t args[5];

    // --- CAS 1 & 2 ---
    printf(BOLD YELLOW
           "\n--- CAS 1 & 2 : Concurrence d'accès (Lecture/Écriture) ---" RESET "\n");
    for (int i = 0; i < 3; i++) {
        args[i].addr = page_ptr;
        args[i].thread_id = i;
        if (i == 2) {
            pthread_create(&threads[i], NULL, thread_write_upgrade, &args[i]);
        } else {
            pthread_create(&threads[i], NULL, thread_read_concurrency, &args[i]);
        }
    }

    for (int i = 0; i < 3; i++)
        pthread_join(threads[i], NULL);

    // --- CAS 3 ---
    printf(BOLD YELLOW "\n--- CAS 3 : Test des verrous exclusifs (LOCK_REQ) ---" RESET
                       "\n");
    for (int i = 3; i < 5; i++) {
        args[i].addr = page_ptr;
        args[i].thread_id = i;
        pthread_create(&threads[i], NULL, thread_lock_test, &args[i]);
    }

    for (int i = 3; i < 5; i++)
        pthread_join(threads[i], NULL);

    printf(BOLD WHITE "\n[SYSTÈME] Nettoyage et fin du programme." RESET "\n");
    dsm_finalize();
    return 0;
}