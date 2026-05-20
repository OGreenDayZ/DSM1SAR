#include <assert.h>

#include "dsm.h"
#include "internal.h"
#include "util/concurrent/thread_pool.h"
#include "util/test_commun.h"

int mock_discover_return_value = 0;
int mock_daemon_return_value = 0;

int count_discover_called = 0;
int count_daemon_called = 0;

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

void test_error_nb_pages_zero() {
    dsm_config config;
    setup_valid_config(&config);

    LOG_WARN("Injection d'erreur : nb_pages = 0");
    config.pager.nb_pages = 0;

    int ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret, -DSM_ERROR_INIT_ARG_NB_PAGES);
    assert(ret == -DSM_ERROR_INIT_ARG_NB_PAGES && "Devrait échouer si nb_pages == 0");

    LOG_SUCCESS("Validation rejetée correctement pour nb_pages == 0");
}

void test_error_page_size_not_power_of_two() {
    dsm_config config;
    setup_valid_config(&config);

    // Cas 1
    LOG_WARN("Injection d'erreur : page_size = 4000 (Non puissance de 2)");
    config.pager.page_size = 4000;
    int ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret, -DSM_ERROR_INIT_ARG_PAGE_SIZE);
    assert(ret == -DSM_ERROR_INIT_ARG_PAGE_SIZE &&
           "Devrait échouer si page_size n'est pas une puissance de 2");

    // Cas 2
    LOG_WARN("Injection d'erreur : page_size = 0");
    config.pager.page_size = 0;
    ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret, -DSM_ERROR_INIT_ARG_PAGE_SIZE);
    assert(ret == -DSM_ERROR_INIT_ARG_PAGE_SIZE && "Devrait échouer si page_size == 0");

    LOG_SUCCESS("Validation rejetée correctement pour les tailles de pages invalides");
}

void test_error_nb_nodes_too_low() {
    dsm_config config;
    setup_valid_config(&config);

    LOG_WARN("Injection d'erreur : nb_nodes = 1 (Minimum requis : 2)");
    config.net.system.nb_nodes = 1;

    int ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret, -DSM_ERROR_INIT_ARG_NB_NODES);
    assert(ret == -DSM_ERROR_INIT_ARG_NB_NODES && "Devrait échouer si nb_nodes < 2");

    LOG_SUCCESS("Validation rejetée correctement pour nb_nodes < 2");
}

void test_error_nb_managers_invalid() {
    dsm_config config;
    setup_valid_config(&config);

    // Cas 1
    LOG_WARN("Injection d'erreur : nb_managers = 0");
    config.net.system.nb_managers = 0;
    int ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret, -DSM_ERROR_INIT_ARG_NB_MANAGERS);
    assert(ret == -DSM_ERROR_INIT_ARG_NB_MANAGERS &&
           "Devrait échouer si nb_managers == 0");

    // Cas 2
    LOG_WARN("Injection d'erreur : nb_managers >= nb_nodes (3 managers pour 3 noeuds)");
    config.net.system.nb_nodes = 3;
    config.net.system.nb_managers = 3;
    ret = validate_config(&config);
    LOG_INFO("Code de retour : %d (Attendu : %d)", ret, -DSM_ERROR_INIT_ARG_NB_MANAGERS);
    assert(ret == -DSM_ERROR_INIT_ARG_NB_MANAGERS &&
           "Devrait échouer si nb_managers >= nb_nodes");

    LOG_SUCCESS(
        "Validation rejetée correctement pour configurations de managers invalides");
}

// ---------------------------------------------------------
// 2. Tests des auto-corrections silencieuses
// ---------------------------------------------------------

void test_auto_correction_thread_pool() {
    dsm_config config;
    setup_valid_config(&config);

    LOG_INFO("Forçage des valeurs du thread pool à 0 pour tester l'auto-correction...");
    config.net.node.pool_nb_threads = 0;
    config.net.node.pool_queue_size = 0;

    int ret = validate_config(&config);

    LOG_INFO("Code de retour : %d (Attendu : 0)", ret);
    assert(ret == 0 && "La configuration devrait être valide");

    LOG_INFO("pool_nb_threads corrigé : %lu (Attendu : %lu)",
             config.net.node.pool_nb_threads, DSM_DEFAULT_NB_THREADS);
    assert(config.net.node.pool_nb_threads == DSM_DEFAULT_NB_THREADS &&
           "Le pool_nb_threads devrait être corrigé avec la valeur par défaut");

    LOG_INFO("pool_queue_size corrigé : %lu (Attendu : %d)",
             config.net.node.pool_queue_size, DSM_DEFAULT_QUEUE_SIZE);
    assert(config.net.node.pool_queue_size == DSM_DEFAULT_QUEUE_SIZE &&
           "Le pool_queue_size devrait être corrigé avec la valeur par défaut");

    LOG_SUCCESS("Auto-correction du thread pool effectuée avec succès");
}

// ---------------------------------------------------------
// 3. Tests des dsm_init
// ---------------------------------------------------------

void test_dsm_init_manager_success() {
    dsm_config config;
    setup_default_config(&config, DSM_ROLE_MANAGER);

    LOG_INFO("Initialisation de dsm_init en tant que MANAGER...");
    dsm_node_role role = dsm_init(&config);

    LOG_INFO("Rôle obtenu : %d (Attendu : %d)", role, DSM_ROLE_MANAGER);
    assert(role == DSM_ROLE_MANAGER && "Le rôle retourné doit être MANAGER");

    LOG_INFO("Appels dsm_discover : %d (Attendu : 1)", count_discover_called);
    assert(count_discover_called == 1 && "dsm_discover aurait dû être appelé 1 fois");

    LOG_INFO("Appels dsm_daemon_start : %d (Attendu : 1)", count_daemon_called);
    assert(count_daemon_called == 1 && "Le daemon du manager aurait dû démarrer");
    if (dsm_ctx != NULL) {
        dsm_safe_map_destroy(&dsm_ctx->nodes_reg);
        free(dsm_ctx);
        dsm_ctx = NULL;
    }
    LOG_SUCCESS("Initialisation du Manager réussie et flux respecté");
}

void test_dsm_init_worker_success() {
    dsm_config config;
    setup_default_config(&config, DSM_ROLE_WORKER);

    LOG_INFO("Initialisation de dsm_init en tant que WORKER...");
    dsm_node_role role = dsm_init(&config);

    LOG_INFO("Rôle obtenu : %d (Attendu : %d)", role, DSM_ROLE_WORKER);
    assert(role == DSM_ROLE_WORKER && "Le rôle retourné doit être WORKER");

    LOG_INFO("Appels dsm_discover : %d (Attendu : 1)", count_discover_called);
    assert(count_discover_called == 1 && "dsm_discover aurait dû être appelé");

    LOG_INFO("Appels dsm_daemon_start direct : %d (Attendu : 0)", count_daemon_called);
    assert(count_daemon_called == 0 &&
           "Le daemon n'est pas appelé directement par le main thread pour le worker");

    if (dsm_ctx != NULL) {
        dsm_safe_map_destroy(&dsm_ctx->nodes_reg);
        free(dsm_ctx);
        dsm_ctx = NULL;
    }
    LOG_SUCCESS("Initialisation du Worker réussie et flux respecté");
}

void test_dsm_init_discover_fails() {
    dsm_config config;
    setup_default_config(&config, DSM_ROLE_MANAGER);

    LOG_WARN("Simulation d'un échec réseau (mock_discover_return_value = -1)");
    mock_discover_return_value = -1;

    dsm_node_role role = dsm_init(&config);

    LOG_INFO("Rôle obtenu en cas d'erreur : %d (Attendu : -1)", (int)role);
    assert((int)role == -1 && "L'init doit échouer si discover échoue");
    assert(count_discover_called == 1);
    assert(count_daemon_called == 0 &&
           "Le daemon ne doit pas démarrer si discover échoue");
    assert(dsm_ctx == NULL &&
           "Le contexte dsm_ctx aurait dû être nettoyé (dsm_ctx_cleanup)");

    LOG_SUCCESS(
        "Échec de dsm_discover géré proprement, sans fuite et sans démarrage de démon");
}

// ---------------------------------------------------------
// Main Runner
// ---------------------------------------------------------

int main() {
    LOG_TITLE("Démarrage des tests de configuration DSM");

    LOG_SECTION("Nombre de pages à zero");
    test_error_nb_pages_zero();

    LOG_SECTION("Taille de la page n'est pas une puissance de 2");
    test_error_page_size_not_power_of_two();

    LOG_SECTION("Pas assez de noeuds");
    test_error_nb_nodes_too_low();

    LOG_SECTION("Nombre de managers invalide");
    test_error_nb_managers_invalid();

    LOG_SECTION("Nombre de thread dans le pool à 0");
    test_auto_correction_thread_pool();

    LOG_SECTION("Manager dsm_init succés");
    test_dsm_init_manager_success();

    LOG_SECTION("Worker dsm_init succés");
    test_dsm_init_worker_success();

    LOG_SECTION("Discover fail");
    test_dsm_init_discover_fails();

    LOG_TITLE("Fin de la suite de tests");
    LOG_SUCCESS("Tous les tests sont passés avec succès !");
    return 0;
}