#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "dsm.h"
#include "util/test_commun.h"
#include "util/test_functions.h"

pthread_t threads[4];
thread_test_arg_t args[4];
unsigned long count_uffd;
int count_test = 5;
int test_ok = 0;

int test_lecture_simple(int *shared_data) {
    LOG_SECTION("--- TEST  1 : Lecture Simple ---");
    args[0].addr = shared_data;
    args[0].id = 1;
    args[0].ret = 0;

    test_simple_read(&args[0]);

    if (args[0].ret < 0)
        return -1;

    LOG_SUCCESS("Lecture simple réussie");
    return 0;
}

int test_ecriture_simple(int *shared_data, unsigned int node_id) {
    LOG_SECTION("--- TEST 2 : Écriture Simple ---");
    args[0].addr = shared_data;
    args[0].id = 2;
    args[0].ret = 0;

    test_simple_write(&args[0], node_id);

    if (args[0].ret < 0)
        return -1;

    LOG_SUCCESS("Écriture simple réussie");
    return 0;
}

int test_verrous_exclusifs(int *shared_data) {
    LOG_SECTION("--- TEST 3 : Verrous Exclusifs ---");
    args[0].addr = shared_data;
    args[0].id = 2;
    args[0].ret = 0;

    if (pthread_create(&threads[0], NULL, test_lock_unlock, &args[0]) != 0) {
        LOG_ERROR("Erreur lors de la création du thread");
        return -1;
    }

    pthread_join(threads[0], NULL);

    if (args[0].ret < 0)
        return -1;

    LOG_SUCCESS("Verrous exclusifs réussi");
    return 0;
}

int test_barriere_temporisee(int *shared_data, unsigned int node_id) {
    LOG_SECTION("--- TEST 4 : Barrière Temporisée ---");
    if (node_id == 2) {
        printf("[Nœud 2] Attente de 1 seconde avant la barrière...\n");
        sleep(1);
    }

    dsm_barrier();

    LOG_INFO("[Nœud %d] Barrière franchie", node_id);
    LOG_SUCCESS("Barriere réussi");
    return 0;
}

int test_coherence_multi_noeuds(int *shared_data, unsigned int node_id) {
    LOG_SECTION("--- TEST 5 : Cohérence Multi-Nœuds ---");
    args[0].addr = shared_data;
    args[0].ret = 0;

    test_coherence_read_write(&args[0], node_id);

    if (args[0].ret < 0)
        return -1;

    dsm_barrier();
    LOG_SUCCESS("Test de cohérence réussi");
    return 0;
}

int main(int argc, char const *argv[]) {
    LOG_TITLE(">>> DÉBUT DU TEST  DSM <<<");

    dsm_config config;
    dsm_get_default_config(&config);

    if (argc >= 3) {
        config.net.system.nb_nodes = atoi(argv[1]);
        config.net.system.nb_managers = atoi(argv[2]);
    } else {
        config.net.system.nb_nodes = 4;
        config.net.system.nb_managers = 1;
    }

    LOG_INFO("Configuration : %d nœuds, %d managers", config.net.system.nb_nodes,
             config.net.system.nb_managers);
    unsigned int node_id;
    int ret = dsm_init(&config, &node_id);
    if (ret < 0) {
        LOG_ERROR("dsm_init a échoué");
        return EXIT_FAILURE;
    } else if (ret == DSM_ROLE_MANAGER) {
        LOG_INFO("[Init] Mode MANAGER actif");
        return EXIT_SUCCESS;
    }

    LOG_INFO("Mode WORKER actif (ID: %d)", node_id);

    int *shared_data = (int *)dsm_alloc();
    if (!shared_data) {
        LOG_ERROR("dsm_alloc impossible");
        return EXIT_FAILURE;
    }
    LOG_INFO("Page allouée à l'adresse virtuelle : %p", (void *)shared_data);

    // Enchaînement séquentiel rigoureux façon test_sync
    if (test_lecture_simple(shared_data) < 0)
        goto cleanup;
    else
        test_ok++;

    if (test_ecriture_simple(shared_data, node_id) < 0)
        goto cleanup;
    else
        test_ok++;

    if (test_verrous_exclusifs(shared_data) < 0)
        goto cleanup;
    else
        test_ok++;

    if (test_barriere_temporisee(shared_data, node_id) < 0)
        goto cleanup;
    else
        test_ok++;

    if (test_coherence_multi_noeuds(shared_data, node_id) < 0)
        goto cleanup;
    else
        test_ok++;

    dsm_finalize();

    LOG_TITLE("--- Bilan des Statistiques (Nœud %d) ---", node_id);

    unsigned long data_msg_count = 0;
    unsigned long inva_msg_count = 0;

    dsm_get_uffd_count(&count_uffd);
    dsm_get_data_count(&data_msg_count);
    dsm_get_inva_count(&inva_msg_count);

    LOG_INFO("  • Déclenchements Page Faults (uffd) : %lu", count_uffd);
    LOG_INFO("  • Messages de données reçus (DATA)  : %lu", data_msg_count);
    LOG_INFO("  • Requêtes d'invalidation reçues    : %lu", inva_msg_count);

    LOG_TITLE("Tests réussis : %d/%d", test_ok, count_test)

    return EXIT_SUCCESS;

cleanup:
    dsm_finalize();
    return EXIT_FAILURE;
}