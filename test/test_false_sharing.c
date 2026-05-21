#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "dsm.h"
#include "util/test_commun.h"

int test_ok = 0;
int count_test = 1;
unsigned long data_msg_count = 0;
unsigned long inva_msg_count = 0;
unsigned long count_uffd = 0;

int test_ping_pong(int *shared_data, int node_id) {
    LOG_SECTION("--- TEST 1 : Faux Partage (Ping-Pong - 1000 itérations)  ---");

    dsm_barrier();

    // Nœud 0 et Nœud 1 écrivent sur la MÊME page, mais pas au même index.
    // Cela provoque une invalidation constante chez l'autre (Ping-Pong).
    for (int i = 0; i < 1000; i++) {
        if (node_id == 1) {
            shared_data[0] = shared_data[0] + i;
            usleep(1000); // Pause de 1 milliseconde
        } else if (node_id == 2) {
            shared_data[1] = shared_data[1] + i;
            usleep(1000); // Pause de 1 milliseconde
        }
    }

    if (node_id == 1)
        LOG_INFO("Fin des écriture successives sur shared_data[0] (Valeur final - %d)",
                 shared_data[0]);
    else if (node_id == 2)
        LOG_INFO("Fin des écriture successives sur shared_data[1] (Valeur final - %d)",
                 shared_data[0]);

    printf(BOLD CYAN "--- Bilan des Statistiques TEST 1 (Nœud %d) ---\n" RESET, node_id);

    dsm_get_uffd_count(&count_uffd);
    dsm_get_data_count(&data_msg_count);
    dsm_get_inva_count(&inva_msg_count);

    LOG_INFO("  • Déclenchements Page Faults (uffd) : %lu", count_uffd);
    LOG_INFO("  • Messages de données reçus (DATA)  : %lu", data_msg_count);
    LOG_INFO("  • Requêtes d'invalidation reçues    : %lu", inva_msg_count);

    dsm_barrier();
    return 0;
}

int test_false_sharing_asymetric(int *shared_data, int node_id) {
    LOG_SECTION("--- TEST 2 : 1 Écrivain vs N Lecteurs (même page - 500 itérations) ---");

    int ITERATIONS = 500;

    dsm_reset_uffd_count();
    dsm_reset_inva_count();
    dsm_reset_data_count();
    dsm_barrier();

    if (node_id == 1) {
        // L'écrivain fou (écrit sur l'index 0)
        for (int i = 0; i < ITERATIONS; i++) {
            shared_data[0] = i;
            // Petite pause pour laisser le temps aux lecteurs de récupérer la page
            usleep(100);
        }
    } else {
        volatile int dummy;
        for (int i = 0; i < ITERATIONS; i++) {
            dummy = shared_data[1];
            usleep(100);
        }
    }

    dsm_barrier();

    // Bilan : Le nœud 0 aura envoyé beaucoup d'invalidations (ou forcé le manager à le
    // faire). Les autres nœuds auront un compteur UFFD qui explose.
    if (node_id == 1)
        printf(BOLD CYAN
               "--- Bilan des Statistiques TEST 2 (Nœud %d ECRIVAIN) ---\n" RESET,
               node_id);
    else
        printf(BOLD CYAN
               "--- Bilan des Statistiques TEST 2 (Nœud %d LECTEUR) ---\n" RESET,
               node_id);

    dsm_get_uffd_count(&count_uffd);
    dsm_get_data_count(&data_msg_count);
    dsm_get_inva_count(&inva_msg_count);

    LOG_INFO("  • Déclenchements Page Faults (uffd) : %lu", count_uffd);
    LOG_INFO("  • Messages de données reçus (DATA)  : %lu", data_msg_count);
    LOG_INFO("  • Requêtes d'invalidation reçues    : %lu", inva_msg_count);

    return 0;
}

/**
"Une première exécution avec 1000 itérations a révélé un nombre de Page Faults de 82, bien
inférieur aux 1000 attendus. Cela met en évidence la vélocité du CPU face à la latence
réseau : le thread acquiert le verrou d'écriture et exécute une rafale d'itérations
consécutives avant que l'invalidation du nœud concurrent ne lui parvienne. Le Faux Partage
est bien présent, mais s'exprime par 'lots'. Pour observer un écroulement complet du
système (thrashing pur), il a fallu augmenter la charge à plusieurs millions
d'itérations."
 */

int test_false_sharing_chaos(int *shared_data, int node_id) {
    LOG_SECTION("--- TEST 3 : Excessive Swapping ---");

    int ITERATIONS = 1000;
    struct timespec start, end;

    dsm_reset_uffd_count();
    dsm_reset_data_count();
    dsm_reset_inva_count();
    dsm_barrier();

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        // Faux partage absolu : chaque nœud modifie la même page, mais pas la même case
        shared_data[node_id] = i;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    dsm_barrier();

    long time_taken = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf(BOLD CYAN "--- Bilan des Statistiques TEST 3 (Nœud %d) ---\n" RESET, node_id);

    dsm_get_uffd_count(&count_uffd);
    dsm_get_data_count(&data_msg_count);
    dsm_get_inva_count(&inva_msg_count);

    LOG_INFO("Nœud %d - Temps : %ld ns, UFFD : %lu, DATA MSG : %lu", node_id, time_taken,
             count_uffd, data_msg_count);
    LOG_INFO("  • Déclenchements Page Faults (uffd) : %lu", count_uffd);
    LOG_INFO("  • Messages de données reçus (DATA)  : %lu", data_msg_count);
    LOG_INFO("  • Requêtes d'invalidation reçues    : %lu", inva_msg_count);

    return 0;
}

int main(int argc, char const *argv[]) {

    dsm_config config;
    dsm_get_default_config(&config);

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

    // METTRE TEST ICI
    if (test_ping_pong(shared_data, node_id) < 0)
        goto cleanup;
    else
        test_ok++;

    if (test_false_sharing_asymetric(shared_data, node_id) < 0)
        goto cleanup;
    else
        test_ok++;

    if (test_false_sharing_chaos(shared_data, node_id) < 0)
        goto cleanup;
    else
        test_ok++;
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