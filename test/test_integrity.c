#include <stdlib.h>

#include "dsm.h"
#include "util/test_commun.h"

int test_ok = 0;
int count_test = 2;

int test_compteur_distibue(int *shared_data, int nb_nodes, int target,
                           unsigned int node_id) {
    LOG_SECTION("--- TEST 1 : Test d'intégrité (Compteur à %d) sur shared_data[0] ---",
                target);
    int increments_per_node = target / nb_nodes;
    if (node_id == 1) {
        shared_data[0] = 0;
    }
    dsm_barrier();
    for (int i = 0; i < increments_per_node; i++) {
        dsm_lock(&shared_data[0]);
        shared_data[0]++;
        dsm_unlock(&shared_data[0]);
    }
    dsm_barrier();

    if (shared_data[0] == target) {
        LOG_SUCCESS("Intégrité validée ! Valeur finale : %d", shared_data[0]);
        return 0;
    } else {
        LOG_ERROR("Échec d'intégrité. Attendu: %d, Obtenu: %d", target, shared_data[0]);
        return -1;
    }
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

    if (test_compteur_distibue(shared_data,
                               config.net.system.nb_nodes - config.net.system.nb_managers,
                               20000, node_id) < 0)
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