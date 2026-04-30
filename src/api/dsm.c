#include "dsm.h"

#include "debug/debug.h"
#include "internal.h"
#include "network/daemon.h"
#include "network/network.h"
#include "network/pager/pager_msg.h"
#include "network/worker/worker.h"
#include "pager/pager.h"
#include "util/concurrent/safe_list.h"
#include "util/concurrent/thread_pool.h"
#include <pthread.h>
#include <stdlib.h>

dsm_context *dsm_ctx = NULL;

void dsm_get_default_config(dsm_config *config) {
    // Pour certains champs, si 0 => soulève erreur à l'init (ex:nb_nodes)
    // et #accept si la valeur sera redéfinie en interne si non spécifiée par l'user
    config->pager.nb_pages = DSM_DEFAULT_NB_PAGES;
    config->pager.page_size = DSM_DEFAULT_PAGESIZE;

    config->net.system.system_id = 0;
    config->net.system.nb_managers = 0;
    config->net.system.nb_nodes = 0;
    snprintf(config->net.system.multicast_ip, INET_ADDRSTRLEN, "%s",
             DSM_DEFAULT_MULTICAST_IP);
    config->net.system.multicast_port = DSM_DEFAULT_MULTICAST_PORT;

    config->net.node.role = DSM_ROLE_UNDEFINED;
    config->net.node.tcp_port = 0;        // #accept
    config->net.node.udp_port = 0;        // #accept
    config->net.node.pool_nb_threads = 0; // #accept
    config->net.node.pool_queue_size = 0; // #accept
}

static int is_power_of_two(size_t x) {
    return (x != 0) && ((x & (x - 1)) == 0);
}

static int validate_config(dsm_config *config) {
    if (config->pager.nb_pages == 0) {
        LOG_ERROR("nb_pages doit être > 0");
        return DSM_ERROR_INIT_ARG_NB_PAGES;
    }

    if (!is_power_of_two(config->pager.page_size)) {
        LOG_ERROR("page_size doit être une puissance de 2 et > 0");
        return DSM_ERROR_INIT_ARG_PAGE_SIZE;
    }

    if (config->net.system.nb_nodes < 2) {
        LOG_ERROR("nb_nodes doit être >= 2");
        return DSM_ERROR_INIT_ARG_NB_NODES;
    }

    if (config->net.system.nb_managers == 0 ||
        config->net.system.nb_managers >= config->net.system.nb_nodes) {
        LOG_ERROR("nb_managers doit être > 0 et < nb_nodes");
        return DSM_ERROR_INIT_ARG_NB_MANAGERS;
    }

    if (config->net.node.pool_nb_threads == 0) {
        config->net.node.pool_nb_threads = DSM_DEFAULT_NB_THREADS;
    }

    if (config->net.node.pool_queue_size == 0) {
        config->net.node.pool_queue_size = DSM_DEFAULT_QUEUE_SIZE;
    }

    return 0;
}

static int dsm_ctx_init(const dsm_config *config) {
    LOG_INFO("Initialisation du contexte de DSM ...");

    if (dsm_ctx) {
        LOG_ERROR("DSM déjà initialisé");
        return DSM_ERROR_ALREADY_INIT;
    }

    dsm_ctx = malloc(sizeof(dsm_context));
    if (!dsm_ctx) {
        LOG_ERROR("Erreur malloc : dsm_ctx");
        return DSM_ERROR_CONTEXT_ALLOC;
    }

    dsm_ctx->config = *config;

    int ret = validate_config(&dsm_ctx->config);
    if (ret != 0) {
        return ret;
    }

    dsm_ctx->node_id = 0;

    if (dsm_safe_map_init(&dsm_ctx->nodes_reg, DSM_NODE_HASH_BITS) != 0) {
        LOG_ERROR("Erreur dsm_safe_map_init : nodes_reg");
        dsm_safe_map_destroy(&dsm_ctx->nodes_reg);
        free(dsm_ctx);
        dsm_ctx = NULL;
        return DSM_ERROR_CONTEXT_ALLOC;
    }
    return 0;
}

static void dsm_ctx_cleanup() {
    dsm_node_content *pos;
    unsigned int bkt;
    struct dsm_hlist_node *tmp;

    dsm_hash_safe_for_each_safe(&dsm_ctx->nodes_reg, bkt, tmp, pos, node) {
        dsm_hlist_del_init(&pos->node);
        free(pos);
    }

    dsm_safe_map_destroy(&dsm_ctx->nodes_reg);

    free(dsm_ctx);
    dsm_ctx = NULL;
}

static void print_nodes_reg() {
    dsm_node_content *pos;
    unsigned int bkt;
    struct dsm_hlist_node *tmp;

    LOG_INFO("Mon Registre de Noeuds :");
    dsm_hash_safe_for_each_safe(&dsm_ctx->nodes_reg, bkt, tmp, pos, node) {
        LOG_INFO("ID=%u, FD_SOCK=%u", pos->id, pos->tcp_sock);
    }
}

static void print_convert_reg() {
    dsm_mgr_to_node_content *pos;
    unsigned int bkt;
    struct dsm_hlist_node *tmp;

    LOG_INFO("Mon Registre de liaison ManagerID -> NodeID :");
    dsm_hash_safe_for_each_safe(&dsm_wkr_ctx->managerID_to_nodeID, bkt, tmp, pos, node) {
        LOG_INFO("ManagerID=%u -> NodeID=%u", pos->manager_id, pos->to_node_content->id);
    }
}

void *daemon_thread_start(void *arg) {
    LOG_INFO("Worker : Démarrage du daemon de DSM...");
    if (dsm_daemon_start() < 0) {
        LOG_ERROR("Erreur dsm_daemon_start");
    }
    return NULL;
}

int dsm_init(const dsm_config *config) {
    LOG_INFO("Initialisation de DSM1SAR...");
    int ret = dsm_ctx_init(config);
    if (ret != 0) {
        return ret;
    }

    ret = dsm_network_init();

    dsm_node_role role = dsm_ctx->config.net.node.role;

    print_nodes_reg();

    if (dsm_ctx->config.net.node.role == DSM_ROLE_WORKER) {
        print_convert_reg();
        if (pthread_create(&dsm_wkr_ctx->daemon, NULL, daemon_thread_start, NULL) != 0) {
            LOG_ERROR("Erreur pthread_create : daemon");
            dsm_ctx_cleanup();
            return -1;
        }
    }

    else if (role == DSM_ROLE_MANAGER) {
        LOG_INFO("Manager : Démarrage du daemon de DSM...");
        if (dsm_daemon_start() < 0) {
            LOG_ERROR("Erreur dsm_daemon_start");
        }
        dsm_ctx_cleanup();
    }

    return ret;
}

void *dsm_alloc(void) {
    if (!dsm_wkr_ctx || dsm_wkr_ctx->range_start == 0) {
        LOG_ERROR("Worker context non initialisé");
        return NULL;
    }

    if (dsm_wkr_ctx->page_counter > EFFECTIVE_NB_PAGES) {
        LOG_ERROR("Adresse d'allocation dépasse la taille de la mémoire alloué");
        return NULL;
    }

    uintptr_t debut = (uintptr_t)dsm_wkr_ctx->range_start + dsm_wkr_ctx->page_counter;

    addr_to_page_id(debut);

    LOG_INFO("Allocation de %lu octets à l'adresse %p", EFFECTIVE_PAGESIZE,
             (void *)debut);
    printf("[dsm_alloc] Allocation de la page %d à l'adresse %p\n",
           dsm_wkr_ctx->page_counter, (void *)debut);

    dsm_wkr_ctx->page_counter++;

    // Initialisation des entrées de la map requests_reg
    dsm_requests_reg_entry *map_entry = malloc(sizeof(dsm_requests_reg_entry));
    dsm_requests_reg_entry *obj;

    map_entry->page_id = addr_to_page_id(debut);

    if (dsm_safe_list_init(&map_entry->list_req) < 0) {
        LOG_ERROR("[dsm_alloc] dsm_safe_list_init");
        free(map_entry);
        return NULL;
    }

    // Ajout de l'entrée pour cette page dans la map
    int inserted = dsm_hash_safe_add_unique(&dsm_wkr_ctx->requests_reg, &map_entry->node,
                                            obj, node, page_id, map_entry->page_id);
    if (inserted == 0) {
        // L'entrée existe deja pas besoin d'une nouvelle
        dsm_safe_list_destroy(&map_entry->list_req);
        free(map_entry);
    }

    return (void *)debut;
}

// TODO - majda : est ce que faut ajouter cette requete dans la map de requete ??
int dsm_lock(void *addr) {
    unsigned int page_id = addr_to_page_id((uint64_t)addr);
    int fd_sock = get_mgr_node_socket_for_page(page_id);

    // Je veux lock la page
    // Je dors sur le mutex de ma liste de lock
    // et j'attends qu'on me reveille
    dsm_locks_reg_entry *entry = malloc(sizeof(dsm_locks_reg_entry));
    entry->page_id = page_id;
    entry->locked = 0;

    if (pthread_mutex_init(&entry->mutex, NULL) < 0) {
        LOG_ERROR("[dsm_lock] pthread_mutex_init &entry->mutex");
        return -1;
    }

    if (pthread_cond_init(&entry->cond, NULL) < 0) {
        LOG_ERROR("[dsm_lock] pthread_mutex_init &entry->mutex");
        return -1;
    }

    pthread_mutex_lock(&entry->mutex);
    dsm_safe_list_add_tail(&dsm_wkr_ctx->locks_reg, &entry->node);

    // PROTECTION
    if (send_wkr_req(page_id, LOCK_REQ, fd_sock) < 0) {
        pthread_mutex_unlock(&entry->mutex);
        return -1;
    }
    while (entry->locked == 0)
        pthread_cond_wait(&entry->cond, &entry->mutex);
    pthread_mutex_unlock(&entry->mutex);
    return 0;
}

int dsm_unlock(void *addr) {
    unsigned int page_id = addr_to_page_id((uint64_t)addr);
    int fd_sock = get_mgr_node_socket_for_page(page_id);
    // PROTECTION
    if (send_wkr_req(page_id, UNLOCK_REQ, fd_sock) < 0) {
        return -1;
    }
    return 0;
}

/**
 * @note TODO - majda : a voir si c'est vraiment utile
 *
 * @return int
 */
int dsm_get_node_id() {
    return dsm_ctx->node_id;
}

int dsm_get_role() {
    return dsm_ctx->config.net.node.role;
}
