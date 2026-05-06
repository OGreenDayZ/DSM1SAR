#include "dsm.h"
#include "util/test_commun.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

pthread_t threads[4];
thread_test_arg_t args[4];

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

void main_test_simple(int *shared_data) {
    // LECTURE SIMPLE
    LOG_SECTION("Test simple - LECTURE");
    LOG_INFO("Lecture directe de la première adresse allouée");
    args[0].addr = shared_data;
    args[0].id = 1;
    test_simple_read(&args[0]);

    // ECRITURE SIMPLE
    LOG_SECTION("Test simple - ÉCRITURE");
    args[0].addr = shared_data;
    args[0].id = 2;
    test_simple_write(&args[0]);

    // LOCK SIMPLE
    LOG_SECTION("Test simple - VERROUS EXCLUSIFS");
    LOG_INFO("Lancement d'un thread dsm_lock/dsm_unlock");
    pthread_create(&threads[0], NULL, test_lock_unlock, &args[0]);
    pthread_join(threads[0], NULL);
    LOG_SUCCESS("Test verrou terminés");

    // BARRIERE SIMPLE
    LOG_SECTION("Test simple - BARRIERE");
    LOG_INFO("Arrive à la barriere (en attente)");
    if (dsm_get_node_id() == 2) {
        LOG_INFO("Je suis le noeud %d et je vais attendre 10 secondes avant de passer la "
                 "barriere!",
                 dsm_get_node_id());
        sleep(1);
    }
    dsm_barrier();

    LOG_INFO(BG_SUCCESS "Barrière franchie !" RESET);
}

int main(int argc, char const *argv[]) {
    LOG_TITLE("  TEST SIMPLE DSM   ");
    LOG_SECTION("Initialisation du contexte DSM");
    dsm_config config;
    dsm_get_default_config(&config);

    if (argc >= 3) {
        config.net.system.nb_nodes = atoi(argv[1]);
        config.net.system.nb_managers = atoi(argv[2]);
    } else {
        config.net.system.nb_nodes = 4;
        config.net.system.nb_managers = 1;
    }

    LOG_INFO("Configuration: %d nœuds, %d managers", config.net.system.nb_nodes,
             config.net.system.nb_managers);

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

    LOG_SECTION("Allocation de mémoire partagée");
    int *shared_data = (int *)dsm_alloc();
    if (!shared_data) {
        LOG_ERROR("Erreur d'allocation DSM");
        return EXIT_FAILURE;
    }
    LOG_SUCCESS("Page allouée à l'adresse : %p", (void *)shared_data);

    main_test_simple(shared_data);

    // FIN
    LOG_SECTION("Nettoyage et fermeture");
    LOG_INFO("Appel de dsm_finalize()");
    dsm_finalize();

    LOG_SUCCESS("Nœud %d : TOUS LES TESTS RÉUSSIS ✓", node_id);
    LOG_TITLE("  FIN DES TESTS DSM  ");
    return EXIT_SUCCESS;
}