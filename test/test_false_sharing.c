#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "dsm.h"
#include "util/test_commun.h"

int test_ok = 0;
int count_test = 5;
unsigned long data_msg_count = 0;
unsigned long inva_msg_count = 0;
unsigned long count_uffd = 0;
/*
Lors du test de "Ping-Pong" sans exclusion mutuelle (Test 1), nous observons une
dégradation de la cohérence de la mémoire partagée, caractérisée par une valeur finale
inférieure au résultat théorique attendu ($497945$ au lieu de $499500$). Ce phénomène
s'explique par le problème classique de la mise à jour perdue (Lost Update), exacerbé par
le faux partage (False Sharing). L'instruction d'incrémentation n'étant pas atomique, un
nœud effectue sa lecture locale juste avant que le protocole DSM basé sur userfaultfd
n'invalide sa page au profit du nœud concurrent. À la reprise du thread, le nœud applique
son écriture en se basant sur un registre CPU obsolète, écrasant ainsi silencieusement les
modifications parallèles. Bien que la temporisation (usleep) réduise la probabilité de
collision, l'introduction des verrous (Test 2) s'avère indispensable pour sérialiser les
accès à la page et garantir une cohérence séquentielle stricte (5/5 tests réussis).
 */
int test_ping_pong(int *shared_data, int node_id) {
    LOG_SECTION("--- TEST 1 : Faux Partage (Ping-Pong - 1000 itérations)  ---");

    if (node_id == 1) {
        shared_data[0] = 0;
        shared_data[1] = 0;
    }

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
    dsm_barrier();

    if (node_id == 1) {
        if (shared_data[0] == 499500) {
            LOG_INFO(
                "Fin des écriture successives sur shared_data[0] (Valeur final - %d)",
                shared_data[0]);
        } else {
            LOG_ERROR("Valeur incohérente (shared_data[0] - %d) (Attendu - %d)",
                      shared_data[0], 499500);
        }
    } else if (node_id == 2) {
        if (shared_data[1] == 499500) {
            LOG_INFO(
                "Fin des écriture successives sur shared_data[1] (Valeur final - %d)",
                shared_data[1]);
        } else {
            LOG_ERROR("Valeur incohérente (shared_data[1] - %d) (Attendu - %d)",
                      shared_data[1], 499500);
        }
    }

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

int test_ping_pong_with_locks(int *shared_data, int node_id) {
    LOG_SECTION(
        "--- TEST 2 : Faux Partage avec locks (Ping-Pong - 1000 itérations)  ---");

    if (node_id == 1) {
        shared_data[0] = 0;
        shared_data[1] = 0;
    }

    // Nœud 0 et Nœud 1 écrivent sur la MÊME page, mais pas au même index.
    // Cela provoque une invalidation constante chez l'autre (Ping-Pong).
    for (int i = 0; i < 1000; i++) {
        if (node_id == 1) {
            dsm_lock(&shared_data[0]);
            shared_data[0] = shared_data[0] + i;
            usleep(1000); // Pause de 1 milliseconde
            dsm_unlock(&shared_data[0]);
        } else if (node_id == 2) {
            dsm_lock(&shared_data[1]);
            shared_data[1] = shared_data[1] + i;
            usleep(1000); // Pause de 1 milliseconde
            dsm_unlock(&shared_data[1]);
        }
    }
    dsm_barrier();

    if (node_id == 1) {
        if (shared_data[0] == 499500) {
            LOG_INFO(
                "Fin des écriture successives sur shared_data[0] (Valeur final - %d)",
                shared_data[0]);
        } else {
            LOG_ERROR("Valeur incohérente (shared_data[0] - %d) (Attendu - %d)",
                      shared_data[0], 499500);
            return -1;
        }
    } else if (node_id == 2) {
        if (shared_data[1] == 499500) {
            LOG_INFO(
                "Fin des écriture successives sur shared_data[1] (Valeur final - %d)",
                shared_data[1]);
        } else {
            LOG_ERROR("Valeur incohérente (shared_data[1] - %d) (Attendu - %d)",
                      shared_data[1], 499500);
            return -1;
        }
    }

    printf(BOLD CYAN "--- Bilan des Statistiques TEST 2 (Nœud %d) ---\n" RESET, node_id);

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
    LOG_SECTION("--- TEST 3 : 1 Écrivain vs N Lecteurs (même page - 500 itérations) ---");

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

    // Bilan : Le nœud 0 aura envoyé beaucoup d'invalidations (ou forcé le manager à
    // le faire). Les autres nœuds auront un compteur UFFD qui explose.
    if (node_id == 1)
        printf(BOLD CYAN
               "--- Bilan des Statistiques TEST 3 (Nœud %d ECRIVAIN) ---\n" RESET,
               node_id);
    else
        printf(BOLD CYAN
               "--- Bilan des Statistiques TEST 3 (Nœud %d LECTEUR) ---\n" RESET,
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
"Une première exécution avec 1000 itérations a révélé un nombre de Page Faults de 82,
bien inférieur aux 1000 attendus. Cela met en évidence la vélocité du CPU face à la
latence réseau : le thread acquiert le verrou d'écriture et exécute une rafale
d'itérations consécutives avant que l'invalidation du nœud concurrent ne lui
parvienne. Le Faux Partage est bien présent, mais s'exprime par 'lots'. Pour observer
un écroulement complet du système (thrashing pur), il a fallu augmenter la charge à
plusieurs millions d'itérations."
 */

int test_false_sharing_chaos(int *shared_data, int node_id) {
    LOG_SECTION("--- TEST 4 : Excessive Swapping ---");

    int ITERATIONS = 1000;
    struct timespec start, end;

    dsm_reset_uffd_count();
    dsm_reset_data_count();
    dsm_reset_inva_count();
    dsm_barrier();

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        // Faux partage absolu : chaque nœud modifie la même page, mais pas la même
        // case
        shared_data[node_id] = i;
        usleep(5000);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    dsm_barrier();

    long time_taken = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf(BOLD CYAN "--- Bilan des Statistiques TEST 5 (Nœud %d) ---\n" RESET, node_id);

    dsm_get_uffd_count(&count_uffd);
    dsm_get_data_count(&data_msg_count);
    dsm_get_inva_count(&inva_msg_count);

    LOG_INFO("Nœud %d - Temps : %ld ns, UFFD : %lu, DATA MSG : %lu", node_id, time_taken,
             count_uffd, data_msg_count);
    LOG_INFO("Valeur final de shared_data[%d] - %d", node_id, shared_data[node_id]);
    LOG_INFO("  • Déclenchements Page Faults (uffd) : %lu", count_uffd);
    LOG_INFO("  • Messages de données reçus (DATA)  : %lu", data_msg_count);
    LOG_INFO("  • Requêtes d'invalidation reçues    : %lu", inva_msg_count);

    return 0;
}

int test_false_sharing_chaos_with_locks(int *shared_data, int node_id) {
    LOG_SECTION("--- TEST 5 : Excessive Swapping with locks ---");

    int ITERATIONS = 1000;
    struct timespec start, end;

    dsm_reset_uffd_count();
    dsm_reset_data_count();
    dsm_reset_inva_count();
    dsm_barrier();

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < ITERATIONS; i++) {
        dsm_lock(&shared_data[node_id]);
        shared_data[node_id] = i;
        dsm_unlock(&shared_data[node_id]);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    dsm_barrier();

    long time_taken = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf(BOLD CYAN "--- Bilan des Statistiques TEST 5 (Nœud %d) ---\n" RESET, node_id);

    dsm_get_uffd_count(&count_uffd);
    dsm_get_data_count(&data_msg_count);
    dsm_get_inva_count(&inva_msg_count);

    LOG_INFO("Nœud %d - Temps : %ld ns, UFFD : %lu, DATA MSG : %lu", node_id, time_taken,
             count_uffd, data_msg_count);
    LOG_INFO("Valeur final de shared_data[%d] - %d", node_id, shared_data[node_id]);
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
        config.net.system.nb_nodes = 4;
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
    if (test_ping_pong_with_locks(shared_data, node_id) < 0)
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

    if (test_false_sharing_chaos_with_locks(shared_data, node_id) < 0)
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
    return EXIT_FAILURE;
}