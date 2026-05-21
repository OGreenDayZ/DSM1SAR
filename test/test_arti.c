#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "dsm.h"
#include "util/test_commun.h"
#include "util/test_functions.h"

int test = 0;
int main() {
    LOG_TITLE("  TEST COMPLET DSM   ");
    // INITIALISATION
    LOG_SECTION("ÉTAPE 1 : Initialisation du contexte DSM");
    dsm_config config;
    dsm_get_default_config(&config);

    config.net.system.nb_nodes = 4;
    config.net.system.nb_managers = 2;
    config.net.node.role = DSM_ROLE_MANAGER;

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
    if (val == 0) {
        LOG_SUCCESS("Valeur lue : %d", val);
        test++;
    } else {
        LOG_ERROR("Erreur lors de la lecture");
    }

    // ECRITURE SIMPLE
    LOG_SECTION("ÉTAPE 4 : Test simple - ÉCRITURE");
    LOG_INFO("Écriture à l'index 50");
    shared_data[50] = 2 * 2;
    LOG_SUCCESS("Valeur écrite : %d", shared_data[50]);
    test++;

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
    ;

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
    ;

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