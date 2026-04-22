#include "dsm.h"

#include <stdio.h>
#include <stdlib.h>

#include "debug/debug.h"
#include "internal.h"
#include "network/network.h"
#include "pager/pager.h"
#include "util/concurrent/safe_map.h"
#include "util/thread_pool.h"

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

    if (config->net.node.role == DSM_ROLE_MANAGER) {
        if (dsm_safe_map_init(&dsm_ctx->node_ctx.manager_ctx.pages_reg,
                              DSM_NODE_HASH_BITS) != 0) {
            LOG_ERROR("Erreur dsm_safe_map_init : manager_ctx.pages_reg");
            free(dsm_ctx);
            dsm_ctx = NULL;
            return DSM_ERROR_CONTEXT_ALLOC;
        }
    } else if (config->net.node.role == DSM_ROLE_WORKER) {
        if (dsm_safe_map_init(&dsm_ctx->node_ctx.worker_ctx.managerID_to_nodeID,
                              DSM_PAGE_HASH_BITS) != 0) {
            LOG_ERROR("Erreur dsm_safe_map_init : worker_ctx.managerID_to_nodeID");
            dsm_safe_map_destroy(&dsm_ctx->nodes_reg);
            free(dsm_ctx);
            dsm_ctx = NULL;
            return DSM_ERROR_CONTEXT_ALLOC;
        }
    }

    if (dsm_safe_map_init(&dsm_ctx->nodes_reg, DSM_PAGE_HASH_BITS) != 0) {
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
    if (dsm_ctx->config.net.node.role == DSM_ROLE_MANAGER)
        dsm_safe_map_destroy(&dsm_ctx->node_ctx.manager_ctx.pages_reg);
    else if (dsm_ctx->config.net.node.role == DSM_ROLE_WORKER)
        dsm_safe_map_destroy(&dsm_ctx->node_ctx.worker_ctx.managerID_to_nodeID);

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

int dsm_init(const dsm_config *config) {
    LOG_INFO("Initialisation de DSM1SAR...");
    int ret = dsm_ctx_init(config);
    if (ret != 0) {
        return ret;
    }

    ret = dsm_network_init();
    print_nodes_reg();
    dsm_ctx_cleanup(); // TODO A ENLEVER SI VRAI TEST DEMON !!!!!!
    // TODO SPLIT THREAD AVANT RETURN
    // thread_pool = pool_create(DSM_DEFAULT_NB_THREADS,
    //   DSM_DEFAULT_QUEUE_SIZE); // TODO : paramètre a revoir
    return ret;
}