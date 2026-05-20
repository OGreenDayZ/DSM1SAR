#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "dsm.h"
#include "util/test_commun.h"
#include "util/test_functions.h"

unsigned long count_uffd;
unsigned long count_data;
unsigned long count_inva;
pthread_t threads[4];
thread_test_arg_t args[4];
int count_test = 1;
int test_ok = 0;

int test_concurrent_write_with_lock(int *shared_data) {
    LOG_SECTION("TEST 1 : Tests multithreads - VERROUS EXCLUSIFS AVEC ECRITURE");
    LOG_INFO("Lancement de 2 threads avec dsm_lock/dsm_unlock et écriture concurrentes "
             "sur shared_data[200]");
    for (int i = 0; i < 2; i++) {
        args[i].addr = &shared_data[200];
        args[i].id = i;
        pthread_create(&threads[i], NULL, test_concurrent_write_with_locks, &args[i]);
    }
    for (int i = 0; i < 2; i++) {
        pthread_join(threads[i], NULL);
        if (args[i].ret < 0)
            return -1;
    }

    dsm_lock(shared_data + 200);
    LOG_SUCCESS("Tests de verrous avec écriture terminés (Valeur finale - %d)",
                shared_data[200]);
    dsm_unlock(shared_data + 200);
    return 0;
}

int main() {
    dsm_config config;
    int node_id;
    if (test_init(&config, &node_id) == 2) {
        return EXIT_SUCCESS;
    }

    int *shared_data = (int *)dsm_alloc();
    if (!shared_data) {
        LOG_ERROR("Erreur d'allocation DSM");
        return EXIT_FAILURE;
    }
    LOG_SUCCESS("Page allouée à l'adresse : %p", (void *)shared_data);

    if (test_concurrent_write_with_lock(shared_data) < 0)
        goto cleanup;
    else
        test_ok++;
    dsm_finalize();
    
    LOG_TITLE("--- Bilan des Statistiques (Nœud %d) ---", dsm_get_node_id());

    // Récupération des compteurs via ton API dsm.h
    dsm_get_uffd_count(&count_uffd);
    dsm_get_data_count(&count_data);
    dsm_get_inva_count(&count_inva);

    LOG_INFO("  • Déclenchements Page Faults (uffd) : %lu", count_uffd);
    LOG_INFO("  • Messages de données reçus (DATA)  : %lu", count_data);
    LOG_INFO("  • Requêtes d'invalidation reçues    : %lu", count_inva);

    LOG_TITLE("Tests réussis : %d/%d", test_ok, count_test)

    return EXIT_SUCCESS;
cleanup:
    dsm_finalize();
    return EXIT_FAILURE;
}
