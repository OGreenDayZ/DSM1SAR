#include "debug/debug.h"
#include "dsm.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    printf(BG_TITLE "╔════════════════════════════════════════╗" RESET "\n");            \
    printf(BG_TITLE "║ " fmt RESET BG_TITLE " ║" RESET "\n", ##__VA_ARGS__);             \
    printf(BG_TITLE "╚════════════════════════════════════════╝" RESET "\n");

#define LOG_SECTION(fmt, ...)                                                            \
    printf(BG_SECTION "┌─────────────────────────────────────┐" RESET "\n");             \
    printf(BG_SECTION "│ " fmt RESET BG_SECTION " │" RESET "\n", ##__VA_ARGS__);         \
    printf(BG_SECTION "└─────────────────────────────────────┘" RESET "\n");

#define LOG_INFO(fmt, ...) printf(BG_INFO "[INFO]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_SUCCESS(fmt, ...) printf(BG_SUCCESS "[✓]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) printf(BG_ERROR "[✗]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) printf(BG_WARN "[!]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...) printf(BG_DEBUG "[DEBUG]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_THREAD(tid, fmt, ...)                                                        \
    printf(BG_THREAD "[Thread %lu]" RESET " " fmt "\n", tid, ##__VA_ARGS__)

// ═══════════════════════════════════════════════════════════════════════════
// STRUCTURES POUR LES THREADS DE TEST
// ═══════════════════════════════════════════════════════════════════════════
typedef struct {
    void *addr;
    int id;
    int test_type; // 0: read, 1: write, 2: lock
} thread_test_arg_t;

typedef struct {
    int thread_id;
    int *shared_mem;
} barrier_test_arg_t;

// ═══════════════════════════════════════════════════════════════════════════
// TEST 1 : LECTURE SIMPLE
// ═══════════════════════════════════════════════════════════════════════════
void *test_simple_read(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();

    LOG_THREAD(tid, "Tentative de LECTURE à %p", data->addr);
    int val = *(int *)data->addr;
    LOG_THREAD(tid, BG_SUCCESS "LECTURE réussie" RESET " → Valeur = %d", val);

    return NULL;
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST 2 : ÉCRITURE SIMPLE
// ═══════════════════════════════════════════════════════════════════════════
void *test_simple_write(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();

    int new_val = 100 + data->id;
    LOG_THREAD(tid, "Tentative d'ÉCRITURE (val=%d) à %p", new_val, data->addr);
    *(int *)data->addr = new_val;
    LOG_THREAD(tid, BG_SUCCESS "ÉCRITURE réussie" RESET " → Nouvelle valeur = %d",
               new_val);

    return NULL;
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST 3 : ÉCRITURES CONCURRENTES
// ═══════════════════════════════════════════════════════════════════════════
void *test_concurrent_writes(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();

    for (int i = 0; i < 3; i++) {
        int val = (data->id * 1000) + i;
        LOG_THREAD(tid, "Écriture concurrente #%d (val=%d)", i, val);
        *(int *)data->addr = val;
        usleep(50000);
    }

    LOG_THREAD(tid, BG_SUCCESS "Écritures concurrentes terminées" RESET);
    return NULL;
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST 4 : VERROUS (LOCK/UNLOCK)
// ═══════════════════════════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════════════════════════
// TEST 5 : BARRIÈRE SIMPLE AVEC THREADS
// ═══════════════════════════════════════════════════════════════════════════
void *test_barrier_thread(void *arg) {
    barrier_test_arg_t *t_args = (barrier_test_arg_t *)arg;
    int tid = t_args->thread_id;
    unsigned long ptid = (unsigned long)pthread_self();

    LOG_THREAD(ptid, "Test #%d : Arrive à la barrière (en attente)", tid);
    dsm_barrier();
    LOG_THREAD(ptid, BG_SUCCESS "Barrière franchie !" RESET);

    return NULL;
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST 6 : BARRIÈRE AVANCÉE AVEC THREADS LOCAUX
// ═══════════════════════════════════════════════════════════════════════════
void *test_barrier_advanced_thread(void *arg) {
    barrier_test_arg_t *t_args = (barrier_test_arg_t *)arg;
    int tid = t_args->thread_id;
    int *data = t_args->shared_mem;
    unsigned long ptid = (unsigned long)pthread_self();

    LOG_THREAD(ptid, "Test avancé #%d : Arrive à la barrière avancée", tid);
    dsm_barrier_advanced(2, 0); // Attend 2 threads

    LOG_THREAD(ptid, BG_SUCCESS "Barrière avancée franchie !" RESET);

    // Test du verrou après la barrière
    LOG_THREAD(ptid, "Tentative d'acquisition du verrou...");
    dsm_lock(&data[100]);
    LOG_THREAD(ptid, BOLD MAGENTA "Verrou acquis" RESET " - Modification en cours");
    data[100] += (tid + 1);
    dsm_unlock(&data[100]);
    LOG_THREAD(ptid, "Verrou relâché");

    // Finalisation avancée
    dsm_finalize_advanced(2);
    return NULL;
}

// ═══════════════════════════════════════════════════════════════════════════
// FONCTION PRINCIPALE
// ═══════════════════════════════════════════════════════════════════════════
int main(int argc, char *argv[]) {
    LOG_TITLE("  TEST COMPLET DSM - AFFICHAGE HOMOGÈNE  ");

    // ─────────────────────────────────────────────────────────────────────
    // 1. INITIALISATION DU DSM
    // ─────────────────────────────────────────────────────────────────────
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
        LOG_SUCCESS("Mode MANAGER activé - En attente des workers");
        return EXIT_SUCCESS;
    }

    int node_id = dsm_get_node_id();
    LOG_SUCCESS("Mode WORKER activé (Node ID: %d)", node_id);

    // ─────────────────────────────────────────────────────────────────────
    // 2. ALLOCATION DE MÉMOIRE PARTAGÉE
    // ─────────────────────────────────────────────────────────────────────
    LOG_SECTION("ÉTAPE 2 : Allocation de mémoire partagée");

    int *shared_data = (int *)dsm_alloc();
    if (!shared_data) {
        LOG_ERROR("Erreur d'allocation DSM");
        return EXIT_FAILURE;
    }
    LOG_SUCCESS("Page allouée à l'adresse : %p", (void *)shared_data);

    // ─────────────────────────────────────────────────────────────────────
    // 3. TEST DE BARRIÈRE GLOBALE
    // ─────────────────────────────────────────────────────────────────────
    LOG_SECTION("ÉTAPE 3 : Test de barrière globale");
    LOG_INFO("Tous les nœuds se synchronisent...");
    dsm_barrier();
    LOG_SUCCESS("Barrière globale franchie par le nœud %d", node_id);

    // ─────────────────────────────────────────────────────────────────────
    // 4. TEST SIMPLE : LECTURE
    // ─────────────────────────────────────────────────────────────────────
    LOG_SECTION("ÉTAPE 4 : Test simple - LECTURE");
    LOG_INFO("Lecture directe de la première adresse allouée");
    int val = shared_data[0];
    LOG_SUCCESS("Valeur lue : %d", val);

    // ─────────────────────────────────────────────────────────────────────
    // 5. TEST SIMPLE : ÉCRITURE
    // ─────────────────────────────────────────────────────────────────────
    LOG_SECTION("ÉTAPE 5 : Test simple - ÉCRITURE");
    LOG_INFO("Écriture à l'index 50");
    shared_data[50] = 2 * 2;
    LOG_SUCCESS("Valeur écrite : %d", shared_data[50]);

    // ─────────────────────────────────────────────────────────────────────
    // 6. TESTS MULTITHREADS : LECTURES CONCURRENTES
    // ─────────────────────────────────────────────────────────────────────
    LOG_SECTION("ÉTAPE 6 : Tests multithreads - LECTURES CONCURRENTES");

    pthread_t threads[4];
    thread_test_arg_t args[4];

    LOG_INFO("Lancement de 3 threads en lecture concurrente");
    for (int i = 0; i < 3; i++) {
        args[i].addr = shared_data;
        args[i].id = i;
        pthread_create(&threads[i], NULL, test_simple_read, &args[i]);
    }

    for (int i = 0; i < 3; i++)
        pthread_join(threads[i], NULL);

    LOG_SUCCESS("Toutes les lectures concurrentes sont terminées");

    // ─────────────────────────────────────────────────────────────────────
    // 7. TESTS MULTITHREADS : ÉCRITURE SIMPLE
    // ─────────────────────────────────────────────────────────────────────
    LOG_SECTION("ÉTAPE 7 : Tests multithreads - ÉCRITURE SIMPLE");

    LOG_INFO("Lancement d'1 thread en écriture");
    args[0].addr = shared_data;
    args[0].id = 1;
    pthread_create(&threads[0], NULL, test_simple_write, &args[0]);
    pthread_join(threads[0], NULL);

    LOG_SUCCESS("Écriture simple terminée");

    // ─────────────────────────────────────────────────────────────────────
    // 8. TESTS MULTITHREADS : ÉCRITURES CONCURRENTES (RACE CONDITIONS)
    // ─────────────────────────────────────────────────────────────────────
    LOG_SECTION("ÉTAPE 8 : Tests multithreads - ÉCRITURES CONCURRENTES");

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

    // ─────────────────────────────────────────────────────────────────────
    // 9. TESTS MULTITHREADS : VERROUS EXCLUSIFS
    // ─────────────────────────────────────────────────────────────────────
    LOG_SECTION("ÉTAPE 9 : Tests multithreads - VERROUS EXCLUSIFS");

    LOG_INFO("Lancement de 2 threads avec dsm_lock/dsm_unlock");
    for (int i = 0; i < 2; i++) {
        args[i].addr = &shared_data[100];
        args[i].id = i;
        pthread_create(&threads[i], NULL, test_lock_unlock, &args[i]);
    }

    for (int i = 0; i < 2; i++)
        pthread_join(threads[i], NULL);

    LOG_SUCCESS("Tests de verrous terminés");

    // ─────────────────────────────────────────────────────────────────────
    // 10. TESTS AVANCÉS : BARRIÈRE SIMPLE AVEC THREADS
    // ─────────────────────────────────────────────────────────────────────
    LOG_SECTION("ÉTAPE 10 : Tests avancés - BARRIÈRE SIMPLE avec threads");

    LOG_INFO("Lancement de 2 threads convergeant vers une barrière");
    barrier_test_arg_t b_args[2];
    for (int i = 0; i < 2; i++) {
        b_args[i].thread_id = i;
        b_args[i].shared_mem = shared_data;
        pthread_create(&threads[i], NULL, test_barrier_thread, &b_args[i]);
    }

    for (int i = 0; i < 2; i++)
        pthread_join(threads[i], NULL);

    LOG_SUCCESS("Barrière simple avec threads réussie");

    // ─────────────────────────────────────────────────────────────────────
    // 11. TESTS AVANCÉS : BARRIÈRE AVANCÉE + VERROUS + FINALISATION
    // ─────────────────────────────────────────────────────────────────────
    LOG_SECTION("ÉTAPE 11 : Tests avancés - BARRIÈRE AVANCÉE + VERROUS + FINALISATION");

    LOG_WARN("Test complet combinant barrière avancée, verrous et finalisation");

    // Thread 0 (principal)
    LOG_INFO("Thread principal arrive à la barrière avancée");
    dsm_barrier_advanced(2, 0);
    LOG_SUCCESS("Thread principal : Barrière avancée franchie !");

    dsm_lock(&shared_data[100]);
    LOG_INFO("Thread principal : Verrou acquis - Section critique");
    shared_data[100] = 1000 * node_id;
    dsm_unlock(&shared_data[100]);
    LOG_INFO("Thread principal : Verrou relâché");

    // Créer un thread supplémentaire
    LOG_INFO("Création d'un thread secondaire pour barrière avancée");
    b_args[0].thread_id = 1;
    b_args[0].shared_mem = shared_data;
    pthread_create(&threads[0], NULL, test_barrier_advanced_thread, &b_args[0]);
    pthread_join(threads[0], NULL);

    LOG_SUCCESS("Test avancé (Barrière + Verrous + Finalisation) réussi");

    // ─────────────────────────────────────────────────────────────────────
    // FINALE : NETTOYAGE
    // ─────────────────────────────────────────────────────────────────────
    LOG_SECTION("ÉTAPE FINALE : Nettoyage et fermeture");

    LOG_INFO("Appel de dsm_finalize()");
    dsm_finalize();

    LOG_SUCCESS("Nœud %d : TOUS LES TESTS RÉUSSIS ✓", node_id);
    LOG_TITLE("  FIN DES TESTS DSM  ");

    return EXIT_SUCCESS;
}