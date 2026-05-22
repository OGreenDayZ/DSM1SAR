#include <assert.h>
#include <stdlib.h>

#include "dsm.h"
#include "internal.h"
#include "util/concurrent/thread_pool.h"
#include "util/test_commun.h"

int mock_discover_return_value = 0;
int mock_daemon_return_value = 0;

int count_discover_called = 0;
int count_daemon_called = 0;

int count_test = 8;
int test_ok;

int __wrap_dsm_discover(void) {
    LOG_INFO("[MOCK] dsm_discover appelé.");
    count_discover_called++;
    return mock_discover_return_value;
}

int __wrap_dsm_daemon_start(void) {
    LOG_INFO("[MOCK] dsm_daemon_start appelé (simulation de la boucle infinie annulée).");
    count_daemon_called++;
    return mock_daemon_return_value;
}

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine)(void *), void *arg) {
    LOG_INFO("[MOCK] pthread_create intercepté. Aucun thread réel ne sera lancé.");
    return 0; // Succès simulé
}

int __wrap_pthread_detach(pthread_t thread) {
    LOG_INFO("[MOCK] pthread_detach intercepté.");
    return 0; // Succès simulé
}
// Fonction utilitaire pour préparer une bonne configuration
void setup_valid_config(dsm_config *config) {
    dsm_get_default_config(config);
    config->pager.nb_pages = 256;
    config->pager.page_size = 4096; // Puissance de 2
    config->net.system.nb_nodes = 3;
    config->net.system.nb_managers = 1;
    config->net.node.role = DSM_ROLE_WORKER;
}

void setup_default_config(dsm_config *config, dsm_node_role role) {
    setup_valid_config(config);
    config->net.node.role = role;

    // Reset des compteurs de mock
    count_discover_called = 0;
    count_daemon_called = 0;
    mock_discover_return_value = 0;
    mock_daemon_return_value = 0;
    // pthread_mutex_lock(&restart_ctx.lock);
    // restart_ctx.can_restart = true;
    // pthread_mutex_unlock(&restart_ctx.lock);
    pthread_mutex_lock(&restart_ctx.lock);
    restart_ctx.can_restart = true;
    pthread_cond_signal(&restart_ctx.cond);
    pthread_mutex_unlock(&restart_ctx.lock);
}

// ---------------------------------------------------------
// 1. Tests des erreurs de validation strictes (Bloquantes)
// ---------------------------------------------------------

int test_error_nb_pages_zero() {
    dsm_config config;
    setup_valid_config(&config);

    LOG_WARN("Injection d'erreur : nb_pages = 0");
    config.pager.nb_pages = 0;

    int ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret, -DSM_ERROR_INIT_ARG_NB_PAGES);
    if (!(ret == -DSM_ERROR_INIT_ARG_NB_PAGES)) {
        LOG_ERROR("Devrait échouer si nb_pages == 0");
        return -1;
    }

    LOG_SUCCESS("Validation rejetée correctement pour nb_pages == 0");
    return 0;
}

int test_error_page_size_not_power_of_two() {
    dsm_config config;
    setup_valid_config(&config);

    // Cas 1
    LOG_WARN("Injection d'erreur : page_size = 4000 (Non puissance de 2)");
    config.pager.page_size = 4000;
    int ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret, -DSM_ERROR_INIT_ARG_PAGE_SIZE);
    if (!(ret == -DSM_ERROR_INIT_ARG_PAGE_SIZE)) {
        LOG_ERROR("Devrait échouer si page_size n'est pas une puissance de 2");
        return -1;
    }

    // Cas 2
    LOG_WARN("Injection d'erreur : page_size = 0");
    config.pager.page_size = 0;
    ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret, -DSM_ERROR_INIT_ARG_PAGE_SIZE);
    if (!(ret == -DSM_ERROR_INIT_ARG_PAGE_SIZE)) {
        LOG_ERROR("Devrait échouer si page_size == 0");
        return -1;
    }

    LOG_SUCCESS("Validation rejetée correctement pour les tailles de pages invalides");
    return 0;
}

int test_error_nb_nodes_too_low() {
    dsm_config config;
    setup_valid_config(&config);

    LOG_WARN("Injection d'erreur : nb_nodes = 1 (Minimum requis : 2)");
    config.net.system.nb_nodes = 1;

    int ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret, -DSM_ERROR_INIT_ARG_NB_NODES);
    if (!(ret == -DSM_ERROR_INIT_ARG_NB_NODES)) {
        LOG_ERROR("Devrait échouer si nb_nodes < 2");
        return -1;
    }

    LOG_SUCCESS("Validation rejetée correctement pour nb_nodes < 2");
    return 0;
}

int test_error_nb_managers_invalid() {
    dsm_config config;
    setup_valid_config(&config);

    // Cas 1
    LOG_WARN("Injection d'erreur : nb_managers = 0");
    config.net.system.nb_managers = 0;
    int ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret, -DSM_ERROR_INIT_ARG_NB_MANAGERS);
    if (!(ret == -DSM_ERROR_INIT_ARG_NB_MANAGERS)) {
        LOG_ERROR("Devrait échouer si nb_managers == 0");
        return -1;
    }

    // Cas 2
    LOG_WARN("Injection d'erreur : nb_managers >= nb_nodes (3 managers pour 3 noeuds)");
    config.net.system.nb_nodes = 3;
    config.net.system.nb_managers = 3;
    ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret, -DSM_ERROR_INIT_ARG_NB_MANAGERS);

    if (!(ret == -DSM_ERROR_INIT_ARG_NB_MANAGERS)) {
        LOG_ERROR("Devrait échouer si nb_managers >= nb_nodes");
        return -1;
    }

    LOG_SUCCESS(
        "Validation rejetée correctement pour configurations de managers invalides");
    return 0;
}

// ---------------------------------------------------------
// 2. Tests des auto-corrections silencieuses
// ---------------------------------------------------------

int test_error_thread_pool_invalid() {
    dsm_config config;
    setup_valid_config(&config);

    // Cas 1 : pool_nb_threads à 0
    LOG_WARN("Injection d'erreur : pool_nb_threads = 0");
    config.net.node.pool_nb_threads = 0;
    int ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret,
             -DSM_ERROR_INIT_ARG_POOL_NB_THREADS);
    if (!(ret == -DSM_ERROR_INIT_ARG_POOL_NB_THREADS)) {
        LOG_ERROR("Devrait échouer si pool_nb_threads == 0");
        return -1;
    }

    // Cas 2 : pool_queue_size à 0
    setup_valid_config(&config);
    LOG_WARN("Injection d'erreur : pool_queue_size = 0");
    config.net.node.pool_queue_size = 0;
    ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret,
             -DSM_ERROR_INIT_ARG_POOL_QUEUE_SIZE);
    if (!(ret == -DSM_ERROR_INIT_ARG_POOL_QUEUE_SIZE)) {
        LOG_ERROR("Devrait échouer si pool_queue_size == 0");
        return -1;
    }

    // Cas 3 : Les deux à 0
    setup_valid_config(&config);
    LOG_WARN("Injection d'erreur : pool_nb_threads = 0 et pool_queue_size = 0");
    config.net.node.pool_nb_threads = 0;
    config.net.node.pool_queue_size = 0;
    ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret,
             -DSM_ERROR_INIT_ARG_POOL_NB_THREADS);
    if (!(ret == -DSM_ERROR_INIT_ARG_POOL_NB_THREADS)) {
        LOG_ERROR("Devrait échouer si les deux paramètres sont à 0");
        return -1;
    }

    LOG_SUCCESS(
        "Validation rejetée correctement pour configurations de thread pool invalides");
    return 0;
}

// ---------------------------------------------------------
// 3. Tests des dsm_init
// ---------------------------------------------------------

int test_dsm_init_manager_success() {
    dsm_config config;
    setup_default_config(&config, DSM_ROLE_MANAGER);
    unsigned int node_id = 0;

    LOG_INFO("Initialisation de dsm_init en tant que MANAGER...");
    dsm_node_role role = dsm_init(&config, &node_id);

    LOG_INFO("Rôle obtenu : %d (Attendu : %d)", role, DSM_ROLE_MANAGER);
    if (!(role == DSM_ROLE_MANAGER)) {
        LOG_ERROR("Le rôle retourné doit être MANAGER");
        return -1;
    }

    LOG_INFO("Appels dsm_discover : %d (Attendu : 1)", count_discover_called);
    if (!(count_discover_called == 1)) {
        LOG_ERROR("dsm_discover aurait dû être appelé 1 fois");
        return -1;
    }

    LOG_INFO("Appels dsm_daemon_start : %d (Attendu : 1)", count_daemon_called);
    assert(count_daemon_called == 1 && "Le daemon du manager aurait dû démarrer");
    if (!(count_daemon_called == 1)) {
        LOG_ERROR("Le daemon du manager aurait dû démarrer");
        return -1;
    }

    if (dsm_ctx != NULL) {
        dsm_safe_map_destroy(&dsm_ctx->nodes_reg);
        free(dsm_ctx);
        dsm_ctx = NULL;
    }
    LOG_SUCCESS("Initialisation du Manager réussie et flux respecté");
    return 0;
}

int test_dsm_init_worker_success() {
    dsm_config config;
    setup_default_config(&config, DSM_ROLE_WORKER);
    unsigned int node_id = 0;

    LOG_INFO("Initialisation de dsm_init en tant que WORKER...");
    dsm_node_role role = dsm_init(&config, &node_id);

    LOG_INFO("Rôle obtenu : %d (Attendu : %d)", role, DSM_ROLE_WORKER);
    if (!(role == DSM_ROLE_WORKER)) {
        LOG_ERROR("Le rôle retourné doit être WORKER");
        return -1;
    }

    LOG_INFO("Appels dsm_discover : %d (Attendu : 1)", count_discover_called);
    if (!(count_discover_called == 1)) {
        LOG_ERROR("dsm_discover aurait dû être appelé");
        return -1;
    }

    LOG_INFO("Appels dsm_daemon_start direct : %d (Attendu : 0)", count_daemon_called);
    assert(count_daemon_called == 0 &&
           "Le daemon n'est pas appelé directement par le main thread pour le worker");
    if (!(count_daemon_called == 0)) {
        LOG_ERROR(
            "Le daemon n'est pas appelé directement par le main thread pour le worker");
        return -1;
    }
    if (dsm_ctx != NULL) {
        dsm_safe_map_destroy(&dsm_ctx->nodes_reg);
        free(dsm_ctx);
        dsm_ctx = NULL;
    }
    LOG_SUCCESS("Initialisation du Worker réussie et flux respecté");
    return 0;
}

int test_dsm_init_discover_fails() {
    dsm_config config;
    setup_default_config(&config, DSM_ROLE_MANAGER);
    unsigned int node_id = 0;

    LOG_WARN("Simulation d'un échec réseau (mock_discover_return_value = -1)");
    mock_discover_return_value = -1;

    dsm_node_role role = dsm_init(&config, &node_id);

    LOG_INFO("Rôle obtenu en cas d'erreur : %d (Attendu : -1)", (int)role);
    if (!((int)role == -1)) {
        LOG_ERROR("L'init doit échouer si discover échoue");
        return -1;
    }

    if (!(count_discover_called == 1)) {
        return -1;
    }

    if (!(count_daemon_called == 0)) {
        LOG_ERROR("Le daemon ne doit pas démarrer si discover échoue");
        return -1;
    }
    if (!(dsm_ctx == NULL)) {
        LOG_ERROR("Le contexte dsm_ctx aurait dû être nettoyé (dsm_ctx_cleanup)");
        return -1;
    }
    LOG_SUCCESS(
        "Échec de dsm_discover géré proprement, sans fuite et sans démarrage de démon");
    return 0;
}

// ---------------------------------------------------------
// Main Runner
// ---------------------------------------------------------

int main() {
    LOG_TITLE("Démarrage des tests de configuration DSM");

    LOG_SECTION("--- TEST 1 : Nombre de pages à zero ---");
    if (!test_error_nb_pages_zero())
        test_ok++;
    else
        goto cleanup;

    LOG_SECTION("--- TEST 2 : Taille de la page n'est pas une puissance de 2 ---");
    if (!test_error_page_size_not_power_of_two())
        test_ok++;
    else
        goto cleanup;

    LOG_SECTION("--- TEST 3 : Pas assez de noeuds ---");
    if (!test_error_nb_nodes_too_low())
        test_ok++;
    else
        goto cleanup;

    LOG_SECTION("--- TEST 4 : Nombre de managers invalide ----");
    if (!test_error_nb_managers_invalid())
        test_ok++;
    else
        goto cleanup;

    LOG_SECTION("--- TEST 5 : Nombre de thread dans le pool à 0 ----");
    if (!test_error_thread_pool_invalid())
        test_ok++;
    else
        goto cleanup;

    LOG_SECTION("--- TEST 6 : Manager dsm_init succés ---");
    if (!test_dsm_init_manager_success())
        test_ok++;
    else
        goto cleanup;

    LOG_SECTION("--- TEST 7 : Worker dsm_init succés ---");
    if (!test_dsm_init_worker_success())
        test_ok++;
    else
        goto cleanup;

    LOG_SECTION("--- TEST 8 : Discover fail ---");
    if (!test_dsm_init_discover_fails())
        test_ok++;
    else
        goto cleanup;

    LOG_TITLE("Fin de la suite de tests");
    LOG_SUCCESS("Tous les tests sont passés avec succès !");
    LOG_TITLE("Tests réussis : %d/%d", test_ok, count_test)
    return EXIT_SUCCESS;
cleanup:
    LOG_TITLE("Fin de la suite de tests");
    LOG_TITLE("Tests réussis : %d/%d", test_ok, count_test)
    return EXIT_FAILURE;
}