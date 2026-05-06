#include "dsm.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// ═══════════════════════════════════════════════════════════════════════════
// PALETTE DE COULEURS ANSI UNIFIÉE
// ═══════════════════════════════════════════════════════════════════════════
#define RESET "\x1b[0m"
#define BOLD "\x1b[1m"
#define BRIGHT_BLACK "\x1b[90m"

// Couleurs de base
#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN "\x1b[36m"
#define WHITE "\x1b[37m"

// Combinaisons
#define BG_TITLE BOLD MAGENTA
#define BG_SECTION BOLD CYAN
#define BG_SUCCESS BOLD GREEN
#define BG_ERROR BOLD RED
#define BG_WARN BOLD YELLOW
#define BG_INFO GREEN
#define BG_DEBUG BLUE
#define BG_THREAD CYAN

// ═══════════════════════════════════════════════════════════════════════════
// MACROS DE LOGGING UNIFIÉES
// ═══════════════════════════════════════════════════════════════════════════
#define LOG_TITLE(fmt, ...)                                                              \
    printf(BG_TITLE "╔════════════════════════════════════════" RESET "\n");             \
    printf(BG_TITLE "║ " fmt RESET BG_TITLE " " RESET "\n", ##__VA_ARGS__);              \
    printf(BG_TITLE "╚════════════════════════════════════════" RESET "\n");

#define LOG_SECTION(fmt, ...)                                                            \
    printf(BG_SECTION "┌─────────────────────────────────────" RESET "\n");              \
    printf(BG_SECTION "│ " fmt RESET BG_SECTION " " RESET "\n", ##__VA_ARGS__);          \
    printf(BG_SECTION "└─────────────────────────────────────" RESET "\n");

#define LOG_INFO(fmt, ...) printf(BG_INFO "[INFO]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_SUCCESS(fmt, ...) printf(BG_SUCCESS "[✓]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) printf(BG_ERROR "[✗]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) printf(BG_WARN "[!]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...) printf(BG_DEBUG "[DEBUG]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_THREAD(tid, fmt, ...)                                                        \
    printf(BG_THREAD "[Thread %lu]" RESET " " fmt "\n", tid, ##__VA_ARGS__)

typedef struct {
    void *addr;
    int id;
    int test_type; // 0: read, 1: write, 2: lock
} thread_test_arg_t;

typedef struct {
    int thread_id;
    int *shared_mem;
} barrier_test_arg_t;

void *test_simple_read(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();
    LOG_THREAD(tid, "Tentative de LECTURE à %p", data->addr);
    int val = *(int *)data->addr;
    LOG_THREAD(tid, BG_SUCCESS "LECTURE réussie" RESET " → Valeur = %d", val);

    return NULL;
}

void *test_simple_write(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();

    int new_val = 100 + data->id;
    LOG_THREAD(tid, "Tentative d'ÉCRITURE (val=%d) à %p", new_val, data->addr);
    *(int *)data->addr = new_val;
    LOG_THREAD(tid, BG_SUCCESS "ÉCRITURE réussie" RESET " → Nouvelle valeur = %d",
               new_val);
    LOG_INFO("Verification de la valeur : ");

    return NULL;
}

void *test_concurrent_writes(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();

    for (int i = 0; i < 3; i++) {
        int val = (data->id * 1000) + i;
        LOG_THREAD(tid, "Écriture concurrente #%d (val=%d)", i, val);
        *(int *)data->addr = val;
        // usleep(50000);
    }

    LOG_THREAD(tid, BG_SUCCESS "Écritures concurrentes terminées" RESET);
    return NULL;
}

void *test_lock_unlock(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();

    LOG_THREAD(tid, "Demande de dsm_lock() sur %p", data->addr);

    if (dsm_lock(data->addr) == 0) {
        LOG_THREAD(tid, BOLD MAGENTA "VERROU ACQUIS" RESET " - Section critique");
        sleep(1);
        dsm_unlock(data->addr);
        LOG_THREAD(tid, CYAN "VERROU LIBÉRÉ (dsm_unlock)" RESET);
    } else {
        LOG_ERROR("Thread %lu : Échec du lock !", tid);
    }

    return NULL;
}

void *test_concurrent_writes_with_locks(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();
    for (int i = 0; i < 3; i++) {
        if (dsm_lock(data->addr) == 0) {
            LOG_THREAD(tid, BOLD MAGENTA "VERROU ACQUIS" RESET " - Section critique");
            int val = (data->id * 1000) + i;
            LOG_THREAD(tid, "Écriture concurrente #%d (val=%d)", i, val);
            *(int *)data->addr = val;
            assert(val == *(int *)data->addr);
            dsm_unlock(data->addr);
            LOG_THREAD(tid, CYAN "VERROU LIBÉRÉ (dsm_unlock)" RESET);
        } else {
            LOG_ERROR("Thread %lu : Échec du lock !", tid);
        }
    }
    LOG_THREAD(tid, BG_SUCCESS "Écritures concurrentes sous locks terminées" RESET);
    return NULL;
}

void *test_concurrent_reads_with_locks(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();
    for (int i = 0; i < 3; i++) {
        if (dsm_lock(data->addr) == 0) {
            LOG_THREAD(tid, BOLD MAGENTA "VERROU ACQUIS" RESET " - Section critique");
            LOG_THREAD(tid, "Tentative de LECTURE à %p", data->addr);
            int val = *(int *)data->addr;
            LOG_THREAD(tid, BG_SUCCESS "LECTURE réussie" RESET " → Valeur = %d", val);
            dsm_unlock(data->addr);
            LOG_THREAD(tid, CYAN "VERROU LIBÉRÉ (dsm_unlock)" RESET);
        } else {
            LOG_ERROR("Thread %lu : Échec du lock !", tid);
        }
    }
    LOG_THREAD(tid, BG_SUCCESS "Écritures concurrentes sous locks terminées" RESET);
    return NULL;
}
void *test_barrier_thread(void *arg) {
    barrier_test_arg_t *t_args = (barrier_test_arg_t *)arg;
    int tid = t_args->thread_id;
    unsigned long ptid = (unsigned long)pthread_self();

    LOG_THREAD(ptid, "Test #%d : Arrive à la barrière (en attente)", tid);
    dsm_barrier();
    LOG_THREAD(ptid, BG_SUCCESS "Barrière franchie !" RESET);

    return NULL;
}

void *test_lock_unlock_barrier_thread(void *arg) {
    test_lock_unlock(arg);
    unsigned long ptid = (unsigned long)pthread_self();
    LOG_THREAD(ptid, "Arrive à la barrière (en attente)");
    dsm_barrier();
    LOG_THREAD(ptid, BG_SUCCESS "Barrière franchie !" RESET);

    return NULL;
}

void *test_barrier_advanced_thread(void *arg) {
    thread_test_arg_t *t_args = (thread_test_arg_t *)arg;
    int tid = t_args->id;
    int *data = t_args->addr;
    unsigned long ptid = (unsigned long)pthread_self();

    LOG_THREAD(ptid, BOLD MAGENTA "Arrive à la barrière avancée" RESET);
    dsm_barrier_advanced(3, 0); // Attend 3 threads + le thread principal
    LOG_THREAD(ptid, BG_SUCCESS "Barrière avancée franchie !" RESET);

    // Test du verrou après la barrière
    LOG_THREAD(ptid, "Tentative d'acquisition du verrou...");
    dsm_lock(&data[100]);
    LOG_THREAD(ptid, BOLD MAGENTA "Verrou acquis" RESET " - Modification en cours");
    data[100] += (tid + 1);
    dsm_unlock(&data[100]);
    LOG_THREAD(ptid, BG_SUCCESS "Verrou relâché" RESET);

    LOG_THREAD(ptid, BOLD MAGENTA "Arrive à la barrière avancée" RESET);
    dsm_barrier_advanced(3, 0); // Attend 3 threads + le thread principal
    LOG_THREAD(ptid, BG_SUCCESS "Barrière avancée franchie !" RESET);
    // Finalisation avancée
    LOG_INFO(BG_DEBUG "Appel de dsm_finalize()" RESET);
    dsm_finalize();
    return NULL;
}

int main() {
    LOG_TITLE("  TEST COMPLET DSM   ");
    // INITIALISATION
    LOG_SECTION("ÉTAPE 1 : Initialisation du contexte DSM");
    dsm_config config;
    dsm_get_default_config(&config);

    config.net.system.nb_nodes = 4;
    config.net.system.nb_managers = 2;

    LOG_INFO("Configuration: 4 nœuds, 2 managers");
    int ret = dsm_init(&config);

    if (ret < 0) {
        LOG_ERROR("Erreur fatale : dsm_init a échoué");
        return EXIT_FAILURE;
    } else if (ret == DSM_ROLE_MANAGER) {
        LOG_SUCCESS("Mode MANAGER activé");
        return EXIT_SUCCESS;
    }

    int node_id = dsm_get_node_id();
    LOG_SUCCESS("Mode WORKER activé (Node ID: %d)", node_id);
    // ALLOCATION
    LOG_SECTION("ÉTAPE 2 : Allocation de mémoire partagée");

    int *shared_data = (int *)dsm_alloc();
    if (!shared_data) {
        LOG_ERROR("Erreur d'allocation DSM");
        return EXIT_FAILURE;
    }
    LOG_SUCCESS("Page allouée à l'adresse : %p", (void *)shared_data);

    // LECTURE SIMPLE
    LOG_SECTION("ÉTAPE 3 : Test simple - LECTURE");
    LOG_INFO("Lecture directe de la première adresse allouée");
    int val = shared_data[0];
    LOG_SUCCESS("Valeur lue : %d", val);

    // ECRITURE SIMPLE
    LOG_SECTION("ÉTAPE 4 : Test simple - ÉCRITURE");
    LOG_INFO("Écriture à l'index 50");
    shared_data[50] = 2 * 2;
    LOG_SUCCESS("Valeur écrite : %d", shared_data[50]);

    pthread_t threads[4];
    thread_test_arg_t args[4];

    // ECRITURE CONCURRENTE
    LOG_SECTION("ÉTAPE 5 : Tests multithreads - ÉCRITURES CONCURRENTES");

    LOG_WARN("⚠ Lancement de 3 threads en écriture CONCURRENTE (potentielle race "
             "condition)");
    for (int i = 0; i < 3; i++) {
        args[i].addr = shared_data;
        args[i].id = i + 10;
        pthread_create(&threads[i], NULL, test_concurrent_writes, &args[i]);
    }
    for (int i = 0; i < 3; i++)
        pthread_join(threads[i], NULL);

    LOG_SUCCESS("Écritures concurrentes terminées (Valeur finale : %d)", shared_data[0]);

    // LECTURE CONCURRENTE
    LOG_SECTION("ÉTAPE 6 :  Tests multithreads - LECTURES CONCURRENTES");
    LOG_INFO("Lancement de 3 threads en lecture concurrente");
    for (int i = 0; i < 3; i++) {
        args[i].addr = shared_data;
        args[i].id = i;
        pthread_create(&threads[i], NULL, test_simple_read, &args[i]);
    }

    for (int i = 0; i < 3; i++)
        pthread_join(threads[i], NULL);

    LOG_SUCCESS("Toutes les lectures concurrentes sont terminées");

    // LOCK SIMPLE
    LOG_SECTION("Étape 7 : Test simple - VERROUS EXCLUSIFS");

    LOG_INFO("Lancement d'un thread dsm_lock/dsm_unlock");
    args[0].addr = shared_data;
    args[0].id = 1;
    pthread_create(&threads[0], NULL, test_lock_unlock, &args[0]);
    pthread_join(threads[0], NULL);

    LOG_SUCCESS("Test verrou terminés");

    // LOCK MULTITHREAD
    LOG_SECTION("ÉTAPE 8 : Tests multithreads - VERROUS EXCLUSIFS");

    LOG_INFO("Lancement de 2 threads avec dsm_lock/dsm_unlock");
    for (int i = 0; i < 2; i++) {
        args[i].addr = &shared_data[100];
        args[i].id = i;
        pthread_create(&threads[i], NULL, test_lock_unlock, &args[i]);
    }

    for (int i = 0; i < 2; i++)
        pthread_join(threads[i], NULL);

    LOG_SUCCESS("Tests de verrous terminés");

    // ECRITURE CONCURRENTES SOUS LOCK
    LOG_SECTION("ÉTAPE 9 : Tests multithreads - VERROUS EXCLUSIFS AVEC ECRITURE");
    LOG_INFO("Lancement de 2 threads avec dsm_lock/dsm_unlock et écriture concurrentes");
    for (int i = 0; i < 2; i++) {
        args[i].addr = &shared_data[200];
        args[i].id = i + 10;
        pthread_create(&threads[i], NULL, test_concurrent_writes_with_locks, &args[i]);
    }
    for (int i = 0; i < 2; i++)
        pthread_join(threads[i], NULL);
    LOG_SUCCESS("Tests de verrous avec écriture terminés");

    // LECTURE & ECRITURE SOUS LOCK - un thread qui lit et un thread qui écrit
    LOG_SECTION(
        "ÉTAPE 10 : Tests multithreads - VERROUS EXCLUSIFS AVEC ECRITURE & LECTURE");
    LOG_INFO(
        "Lancement de 2 threads - un en lecture avec lock et un en écriture avec lock");

    args[0].addr = &shared_data[300];
    args[0].id = 0 + 10;
    pthread_create(&threads[0], NULL, test_concurrent_writes_with_locks, &args[0]);
    args[1].addr = &shared_data[300];
    args[1].id = 1 + 10;
    pthread_create(&threads[1], NULL, test_concurrent_reads_with_locks, &args[1]);

    for (int i = 0; i < 2; i++)
        pthread_join(threads[i], NULL);

    // BARRIERE SIMPLE
    LOG_SECTION("ÉTAPE 11 : Test simple - BARRIERE");
    LOG_INFO("Arrive à la barriere (en attente)");
    if (dsm_get_node_id() == 2) {
        LOG_INFO("Je suis le noeud %d et je vais attendre 10 secondes avant de passer la "
                 "barriere!",
                 dsm_get_node_id());
        sleep(1);
    }
    dsm_barrier();
    LOG_INFO(BG_SUCCESS "Barrière franchie !" RESET);

    // BARRIERE SIMPLE AVEC LOCKS
    LOG_SECTION("ÉTAPE 12 : Tests simple - BARRIÈRE SIMPLE avec locks");

    LOG_INFO("Lancement d'un threads convergeant vers une barrière");
    args[0].addr = &shared_data[200];
    args[0].id = 0 + 10;
    pthread_create(&threads[0], NULL, test_lock_unlock_barrier_thread, &args[0]);
    pthread_join(threads[0], NULL);
    LOG_SUCCESS("Barrière simple avec threads et locks réussie");

    // BARRIERE AVANCÉE
    LOG_SECTION("Étape 13 : Tests multithread - BARRIÈRE AVANCÉE")
    LOG_INFO("Lancement de 3 threads effectuant des reads simple");
    for (int i = 0; i < 3; i++) {
        args[i].addr = shared_data;
        args[i].id = i;
        pthread_create(&threads[i], NULL, test_barrier_advanced_thread, &args[i]);
    }

    // for (int i = 0; i < 3; i++) {
    //     pthread_join(threads[i], NULL);
    // }

    LOG_SUCCESS("Barrière avancée avec threads et locks réussie");

    // FIN
    LOG_SECTION("ÉTAPE FINALE : Nettoyage et fermeture");
    LOG_INFO("Appel de dsm_finalize_advanced()");
    dsm_finalize_advanced(4);

    LOG_SUCCESS("Nœud %d : TOUS LES TESTS RÉUSSIS ✓", node_id);
    LOG_TITLE("  FIN DES TESTS DSM  ");
    return EXIT_SUCCESS;
}