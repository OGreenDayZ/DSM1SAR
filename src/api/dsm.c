#include "dsm.h"

#include <bits/pthreadtypes.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "debug/debug.h"
#include "internal.h"
#include "network/daemon.h"
#include "network/discover/net_discover.h"
#include "network/manager/manager.h"
#include "network/network.h"
#include "network/pager/pager_msg.h"
#include "network/worker/sync.h"
#include "network/worker/worker.h"
#include "pager/pager.h"
#include "util/concurrent/drain_queue.h"
#include "util/concurrent/safe_list.h"
#include "util/concurrent/safe_map.h"
#include "util/concurrent/thread_pool.h"
#include "util/container_of.h"
#include "util/hlist.h"
#include "util/list.h"

dsm_context *dsm_ctx = NULL;

void dsm_get_default_config(dsm_config *config) {
    // Pour certains champs, si 0 => soulève erreur à l'init (ex:nb_nodes)
    // et #accept si la valeur sera redéfinie en interne si non spécifiée par l'user
    if (!config) {
        LOG_ERROR("config NULL passé à dsm_get_default_config");
        return;
    }
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
int validate_config(dsm_config *config) {
    if (config->pager.nb_pages == 0) {
        LOG_ERROR("nb_pages doit être > 0");
        return -DSM_ERROR_INIT_ARG_NB_PAGES;
    }

    if (!is_power_of_two(config->pager.page_size)) {
        LOG_ERROR("page_size doit être une puissance de 2 et > 0");
        return -DSM_ERROR_INIT_ARG_PAGE_SIZE;
    }

    if (config->net.system.nb_nodes < 2) {
        LOG_ERROR("nb_nodes doit être >= 2");
        return -DSM_ERROR_INIT_ARG_NB_NODES;
    }

    if (config->net.system.nb_managers == 0 ||
        config->net.system.nb_managers >= config->net.system.nb_nodes) {
        LOG_ERROR("nb_managers doit être > 0 et < nb_nodes");
        return -DSM_ERROR_INIT_ARG_NB_MANAGERS;
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
        return -DSM_ERROR_ALREADY_INIT;
    }

    dsm_ctx = malloc(sizeof(dsm_context));
    if (!dsm_ctx) {
        LOG_ERROR("Erreur malloc : dsm_ctx");
        return -DSM_ERROR_CONTEXT_ALLOC;
    }

    dsm_ctx->config = *config;

    int ret = validate_config(&dsm_ctx->config);
    if (ret < 0) {
        return ret;
    }

    dsm_ctx->node_id = 0;

    if (dsm_safe_map_init(&dsm_ctx->nodes_reg, DSM_NODE_HASH_BITS) != 0) {
        LOG_ERROR("Erreur dsm_safe_map_init : nodes_reg");
        dsm_safe_map_destroy(&dsm_ctx->nodes_reg);
        free(dsm_ctx);
        dsm_ctx = NULL;
        return -DSM_ERROR_CONTEXT_ALLOC;
    }
    return 0;
}

static void dsm_ctx_cleanup() {
    dsm_node_content *pos;
    unsigned int bkt;
    struct dsm_hlist_node *tmp;

    if (!dsm_ctx) {
        LOG_ERROR("DSM non initialisé");
        return;
    }
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

    if (!dsm_ctx) {
        LOG_ERROR("DSM non initialisé");
        return;
    }

    LOG_INFO("Mon Registre de Noeuds :");
    dsm_hash_safe_for_each_safe(&dsm_ctx->nodes_reg, bkt, tmp, pos, node) {
        LOG_INFO("ID=%u, FD_SOCK=%u", pos->id, pos->tcp_sock);
    }
}

static void print_convert_reg() {
    dsm_mgr_to_node_content *pos;
    unsigned int bkt;
    struct dsm_hlist_node *tmp;

    if (!dsm_wkr_ctx) {
        LOG_ERROR("Worker context pas initialisé");
        return;
    }

    LOG_INFO("Mon Registre de liaison ManagerID -> NodeID :");
    dsm_hash_safe_for_each_safe(&dsm_wkr_ctx->managerID_to_nodeID, bkt, tmp, pos, node) {
        LOG_INFO("ManagerID=%u -> NodeID=%u", pos->manager_id, pos->to_node_content->id);
    }
}

void *daemon_thread_start(__attribute__((unused)) void *arg) {
    LOG_INFO("Worker : Démarrage du daemon de DSM...");
    if (dsm_daemon_start() < 0) {
        LOG_ERROR("Erreur dsm_daemon_start");
    }
    dsm_wkr_ctx_cleanup();
    dsm_ctx_cleanup();
    return NULL;
}

dsm_node_role dsm_init(const dsm_config *config) {
    LOG_INFO("Initialisation de DSM1SAR...");
    if (dsm_ctx_init(config) < 0) {
        return -1;
    }

    if (dsm_discover() < 0) {
        LOG_ERROR("Erreur dsm_discover");
        dsm_ctx_cleanup();
        return -1;
    }

    dsm_node_role role = dsm_ctx->config.net.node.role;

    print_nodes_reg();

    if (dsm_ctx->config.net.node.role == DSM_ROLE_WORKER) {
        print_convert_reg();
        pthread_t daemon_thread;
        if (pthread_create(&daemon_thread, NULL, daemon_thread_start, NULL) != 0 ||
            pthread_detach(daemon_thread) != 0) {
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
        dsm_mgr_ctx_cleanup();
        dsm_ctx_cleanup();

    } else { // role == DSM_ROLE_UNDEFINED
        dsm_ctx_cleanup();
        return -1;
    }

    return role;
}

void *dsm_alloc(void) {
    if (!dsm_wkr_ctx || dsm_wkr_ctx->range_start == 0) {
        LOG_ERROR("Worker context non initialisé");
        return NULL;
    }
    if ((unsigned int)dsm_wkr_ctx->page_counter > EFFECTIVE_NB_PAGES) {
        LOG_ERROR("Adresse d'allocation dépasse la taille de la mémoire alloué");
        return NULL;
    }

    dsm_requests_reg_entry *map_reqs_entry = NULL;
    dsm_locks_reg_entry *map_locks_entry = NULL;
    dsm_requests_reg_entry *obj = NULL;
    dsm_locks_reg_entry *obj_locks = NULL;

    map_reqs_entry = malloc(sizeof(dsm_requests_reg_entry));
    map_locks_entry = malloc(sizeof(dsm_locks_reg_entry));

    uintptr_t debut = (uintptr_t)dsm_wkr_ctx->range_start + dsm_wkr_ctx->page_counter;
    LOG_INFO("Allocation de %lu octets à l'adresse %p", EFFECTIVE_PAGESIZE,
             (void *)debut);

    if (!map_reqs_entry || !map_locks_entry) {
        LOG_ERROR("malloc dsm_requests_reg_entry ou dsm_locks_reg_entry");
        goto exit_error;
    }

    map_locks_entry->page_id = dsm_wkr_ctx->page_counter;
    if (dsm_safe_list_init(&map_locks_entry->locks_list) < 0) {
        LOG_ERROR("[dsm_alloc] dsm_safe_list_init map_locks_entry->locks_list");
        goto exit_error;
    }
    map_reqs_entry->page_id = dsm_wkr_ctx->page_counter;
    map_reqs_entry->state = NONE;
    dsm_dq_init(&map_reqs_entry->pending_msgs);

    if (pthread_mutex_init(&map_reqs_entry->mutex, NULL) < 0) {
        LOG_ERROR("[dsm_alloc] pthread_mutex_init");
        goto exit_error;
    }

    // Ajout de l'entrée pour cette page dans la map
    int inserted =
        dsm_hash_safe_add_unique(&dsm_wkr_ctx->requests_reg, &map_reqs_entry->node, obj,
                                 node, page_id, map_reqs_entry->page_id);
    if (inserted == 0) {
        goto exit_error;
    }

    inserted =
        dsm_hash_safe_add_unique(&dsm_wkr_ctx->locks_reg, &map_locks_entry->node,
                                 obj_locks, node, page_id, map_locks_entry->page_id);
    if (inserted == 0) {
        goto exit_error;
    }

    dsm_wkr_ctx->page_counter++;

    return (void *)debut;

exit_error:
    free(map_reqs_entry);
    free(map_locks_entry);
    return NULL;
}

int dsm_lock(void *addr) {
    if (!dsm_wkr_ctx) {
        LOG_ERROR("Worker context non initialisé");
        return -1;
    }

    unsigned int page_id = addr_to_page_id((uint64_t)addr);
    int fd_sock = get_mgr_node_socket_for_page(page_id);
    pthread_mutex_t *send_lock = get_mgr_send_lock_for_page(page_id);
    dsm_locks_reg_entry *locks_reg_entry = NULL, *found = NULL;
    dsm_locks_list_entry *entry_list = malloc(sizeof(dsm_locks_list_entry));

    if (entry_list == NULL) {
        LOG_ERROR("Erreur de malloc pour une entrée de locks_list");
        return -1;
    }
    dsm_hash_safe_get(&dsm_wkr_ctx->locks_reg, locks_reg_entry, node, page_id) {
        if (locks_reg_entry->page_id == page_id) {
            found = locks_reg_entry;
            // dsm_hash_safe_unlock(&dsm_wkr_ctx->locks_reg, page_id);
            break;
        }
    }
    dsm_hash_safe_get_end(&dsm_wkr_ctx->locks_reg, page_id);
    if (found == NULL) {
        LOG_INFO("[dsm_lock] locks_reg est mal initialisé");
        return -1;
    }
    entry_list->locked = 0;
    if (pthread_mutex_init(&entry_list->mutex, NULL) < 0) {
        LOG_ERROR("[dsm_lock] pthread_mutex_init &entry->mutex");
        return -1;
    }
    if (pthread_cond_init(&entry_list->cond, NULL) < 0) {
        LOG_ERROR("[dsm_lock] pthread_cond_init &entry->mutex");
        return -1;
    }

    dsm_safe_list_lock(&found->locks_list);
    dsm_list_add_tail(&found->locks_list.head, &entry_list->node);
    pthread_mutex_lock(send_lock);
    if (send_wkr_req(page_id, LOCK_REQ, fd_sock) < 0) {
        pthread_mutex_unlock(send_lock);
        return -1;
    }
    pthread_mutex_unlock(send_lock);
    dsm_safe_list_unlock(&found->locks_list);

    pthread_mutex_lock(&entry_list->mutex);
    while (entry_list->locked == 0) {
        pthread_cond_wait(&entry_list->cond, &entry_list->mutex);
    }
    pthread_mutex_unlock(&entry_list->mutex);
    return 0;
}

int dsm_unlock(void *addr) {
    if (!dsm_wkr_ctx) {
        LOG_ERROR("Worker context non initialisé");
        return -1;
    }

    unsigned int page_id = addr_to_page_id((uint64_t)addr);
    int fd_sock = get_mgr_node_socket_for_page(page_id);
    pthread_mutex_t *send_lock = get_mgr_send_lock_for_page(page_id);
    dsm_locks_reg_entry *locks_reg_entry = NULL, *found = NULL;
    dsm_locks_list_entry *first_entry = NULL;
    // Faut supprimer l'entrée de ici
    dsm_hash_safe_get(&dsm_wkr_ctx->locks_reg, locks_reg_entry, node, page_id) {
        if (locks_reg_entry->page_id == page_id) {
            found = locks_reg_entry;
            break;
        }
    }
    dsm_hash_safe_get_end(&dsm_wkr_ctx->locks_reg, page_id);

    dsm_safe_list_lock(&found->locks_list);

    if (!dsm_list_empty(&found->locks_list.head)) {
        dsm_list_head *first_node = found->locks_list.head.next;
        first_entry = dsm_container_of(first_node, dsm_locks_list_entry, node);
    }
    dsm_safe_list_unlock(&found->locks_list);

    LOG_INFO("[dsm_lock] J'ai reçu LOCK_ACK");
    dsm_safe_list_del(&found->locks_list, &first_entry->node);
    pthread_mutex_destroy(&first_entry->mutex);
    pthread_cond_destroy(&first_entry->cond);
    free(first_entry);

    pthread_mutex_lock(send_lock);
    if (send_wkr_req(page_id, UNLOCK_REQ, fd_sock) < 0) {
        pthread_mutex_unlock(send_lock);
        return -1;
    }
    pthread_mutex_unlock(send_lock);
    return 0;
}

int dsm_barrier_advanced(unsigned int threads_count, unsigned int manager_id) {
    if (!dsm_wkr_ctx) {
        LOG_ERROR("Worker context non initialisé");
        return -1;
    }

    pthread_mutex_lock(&dsm_wkr_ctx->sync.barr.lock);

    if (barrier_define(&dsm_wkr_ctx->sync.barr, threads_count, manager_id) < 0) {
        goto error_unlock;
    }

    if (barrier_wait(&dsm_wkr_ctx->sync.barr) < 0) {
        goto error_unlock;
    }

    pthread_mutex_unlock(&dsm_wkr_ctx->sync.barr.lock);
    return 0;

error_unlock:
    pthread_mutex_unlock(&dsm_wkr_ctx->sync.barr.lock);
    return -1;
}

int dsm_barrier(void) {
    return dsm_barrier_advanced(1, 0);
}

int dsm_finalize_advanced(unsigned int threads_count) {
    if (!dsm_wkr_ctx) {
        LOG_ERROR("Worker context non initialisé");
        return -1;
    }

    pthread_mutex_lock(&dsm_wkr_ctx->sync.fina.lock);

    if (finalize_define(&dsm_wkr_ctx->sync.fina, threads_count) < 0) {
        goto error_unlock;
    }

    if (finalize_wait(&dsm_wkr_ctx->sync.fina) < 0) {
        goto error_unlock;
    }

    pthread_mutex_unlock(&dsm_wkr_ctx->sync.fina.lock);
    return 0;

error_unlock:
    pthread_mutex_unlock(&dsm_wkr_ctx->sync.fina.lock);
    return -1;
}

int dsm_finalize(void) {
    return dsm_finalize_advanced(1);
}

/**
 * @note TODO - majda : a voir si c'est vraiment utile
 *
 * @return int
 */
int dsm_get_node_id() {
    if (!dsm_ctx) {
        LOG_ERROR("DSM non initialisé");
        return -1;
    }
    return dsm_ctx->node_id;
}

bool dsm_get_uffd_count(unsigned long *out) {
    if (!dsm_wkr_ctx) {
        LOG_ERROR("Worker context non initialisé");
        return false;
    }
    *out = atomic_load(&dsm_wkr_ctx->uffd_count);
    return true;
}

bool dsm_get_data_count(unsigned long *out) {
    if (!dsm_wkr_ctx) {
        LOG_ERROR("Worker context non initialisé");
        return false;
    }
    *out = atomic_load(&dsm_wkr_ctx->data_count);
    return true;
}

bool dsm_get_inva_count(unsigned long *out) {
    if (!dsm_wkr_ctx) {
        LOG_ERROR("Worker context non initialisé");
        return false;
    }
    *out = atomic_load(&dsm_wkr_ctx->inva_count);
    return true;
}

bool dsm_reset_uffd_count() {
    if (!dsm_wkr_ctx) {
        LOG_ERROR("Worker context non initialisé");
        return false;
    }
    atomic_store(&dsm_wkr_ctx->uffd_count, 0);
    return true;
}

bool dsm_reset_data_count() {
    if (!dsm_wkr_ctx) {
        LOG_ERROR("Worker context non initialisé");
        return false;
    }
    atomic_store(&dsm_wkr_ctx->data_count, 0);
    return true;
}

bool dsm_reset_inva_count() {
    if (!dsm_wkr_ctx) {
        LOG_ERROR("Worker context non initialisé");
        return false;
    }
    atomic_store(&dsm_wkr_ctx->inva_count, 0);
    return true;
}
