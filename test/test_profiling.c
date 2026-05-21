#include <stdlib.h>
#include <time.h>

#include "dsm.h"
#include "util/test_commun.h"

int test_ok = 0;
int count_test = 2;

int test_mesure_surcout(int *shared_data, unsigned int node_id) {
    LOG_SECTION("--- TEST 2 : Surcoût d'un Page Fault ---");
    struct timespec start, end;
    long time_local, time_dsm;

    // 1. Accès RAM Classique
    int *local_data = malloc(sizeof(int));
    clock_gettime(CLOCK_MONOTONIC, &start);
    local_data[0] = 42;
    clock_gettime(CLOCK_MONOTONIC, &end);
    time_local = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    free(local_data);

    dsm_barrier();

    // 2. Accès DSM (Forcer un page fault)
    if (node_id == 1) {
        shared_data[0] = 42; // Nœud 0 prend l'ownership
    }
    dsm_barrier();

    // Le nœud 1 lit, ce qui va déclencher l'UFFD, demander la page au Manager, etc.
    clock_gettime(CLOCK_MONOTONIC, &start);
    volatile int val = shared_data[0];
    LOG_INFO("Valeur lu sous locks (shared_data[0]) : %d", val);
    clock_gettime(CLOCK_MONOTONIC, &end);

    time_dsm = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);

    LOG_INFO("Temps accès RAM local : %ld ns", time_local);
    LOG_INFO("Temps accès Page Fault DSM : %ld ns", time_dsm);
    LOG_INFO("Surcoût réseau + UFFD : x%.2f", (double)time_dsm / time_local);

    return 0;
}

int test_mesure_surcout_avec_lock(int *shared_data, unsigned int node_id) {
    LOG_SECTION("--- TEST 3 : Surcoût d'un Page Fault avec dsm_lock ---");
    struct timespec start, end;
    long time_local, time_dsm;

    // 1. Accès RAM Classique
    int *local_data = malloc(sizeof(int));
    clock_gettime(CLOCK_MONOTONIC, &start);
    local_data[0] = 42;
    clock_gettime(CLOCK_MONOTONIC, &end);
    time_local = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    free(local_data);

    dsm_barrier();

    // 2. Accès DSM (Forcer un page fault)
    if (node_id == 1) {
        shared_data[0] = 42; // Nœud 0 prend l'ownership
    }
    dsm_barrier();

    // Le nœud 1 lit, ce qui va déclencher l'UFFD, demander la page au Manager, etc.
    clock_gettime(CLOCK_MONOTONIC, &start);
    dsm_lock(&shared_data[0]);
    volatile int val = shared_data[0];
    LOG_INFO("Valeur lu sous locks (shared_data[0]) : %d", val);
    dsm_unlock(&shared_data[0]);
    clock_gettime(CLOCK_MONOTONIC, &end);

    time_dsm = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);

    LOG_INFO("Temps accès RAM local : %ld ns", time_local);
    LOG_INFO("Temps accès Page Fault DSM : %ld ns", time_dsm);
    LOG_INFO("Surcoût réseau + UFFD : x%.2f", (double)time_dsm / time_local);

    return 0;
}

int main(int argc, char const *argv[]) {

    dsm_config config;
    dsm_get_default_config(&config);
    unsigned long data_msg_count = 0;
    unsigned long inva_msg_count = 0;
    unsigned long count_uffd = 0;

    if (argc >= 3) {
        config.net.system.nb_nodes = atoi(argv[1]);
        config.net.system.nb_managers = atoi(argv[2]);
    } else {
        config.net.system.nb_nodes = 6;
        config.net.system.nb_managers = 1;
    }

    unsigned int node_id;
    int ret = dsm_init(&config, &node_id);
    if (ret < 0) {
        LOG_ERROR("dsm_init a échoué");
        return EXIT_FAILURE;
    }
    LOG_TITLE(">>> DSM TEST D'INTEGRITE (Noeud %d) <<<", node_id);
    LOG_INFO("Configuration : %d nœuds, %d managers", config.net.system.nb_nodes,
             config.net.system.nb_managers);
    if (ret == DSM_ROLE_MANAGER) {
        LOG_INFO("[Init] Mode MANAGER actif");
        return EXIT_SUCCESS;
    }

    int *shared_data = (int *)dsm_alloc();
    if (!shared_data) {
        LOG_ERROR("dsm_alloc impossible");
        return EXIT_FAILURE;
    }
    LOG_INFO("Page allouée à l'adresse virtuelle : %p", (void *)shared_data);

    if (test_mesure_surcout(shared_data, node_id) < 0)
        goto cleanup;
    else
        test_ok++;

    if (test_mesure_surcout_avec_lock(shared_data, node_id) < 0)
        goto cleanup;
    else
        test_ok++;
    LOG_TITLE("--- Bilan des Statistiques (Nœud %d) ---", node_id);

    dsm_get_uffd_count(&count_uffd);
    dsm_get_data_count(&data_msg_count);
    dsm_get_inva_count(&inva_msg_count);

    LOG_INFO("  • Déclenchements Page Faults (uffd) : %lu", count_uffd);
    LOG_INFO("  • Messages de données reçus (DATA)  : %lu", data_msg_count);
    LOG_INFO("  • Requêtes d'invalidation reçues    : %lu", inva_msg_count);

    LOG_TITLE("Tests réussis : %d/%d", test_ok, count_test)

    dsm_finalize();
    return EXIT_SUCCESS;

cleanup:
    LOG_TITLE("--- Bilan des Statistiques (Nœud %d) ---", node_id);

    dsm_get_uffd_count(&count_uffd);
    dsm_get_data_count(&data_msg_count);
    dsm_get_inva_count(&inva_msg_count);

    LOG_INFO("  • Déclenchements Page Faults (uffd) : %lu", count_uffd);
    LOG_INFO("  • Messages de données reçus (DATA)  : %lu", data_msg_count);
    LOG_INFO("  • Requêtes d'invalidation reçues    : %lu", inva_msg_count);

    LOG_TITLE("Tests réussis : %d/%d", test_ok, count_test)

    dsm_finalize();
    dsm_finalize();
    return EXIT_FAILURE;
}