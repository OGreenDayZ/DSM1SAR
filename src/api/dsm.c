#include "dsm.h"

#include <stdio.h>
#include <stdlib.h>

#include "debug/debug.h"
#include "internal.h"
#include "network/network.h"
#include "pager/pager.h"
#include "util/hashmap.h"

dsm_context *dsm_ctx = NULL;

static int is_power_of_two(size_t x) {
    return (x != 0) && ((x & (x - 1)) == 0);
}

static int validate_config(const dsm_config *config) {
    if (config->pager.nb_pages == 0) {
        LOG_ERROR("nb_pages doit être > 0");
        return DSM_ERROR_INIT_ARG_NB_PAGES;
    }

    if (!is_power_of_two(config->pager.page_size)) {
        LOG_ERROR("page_size doit être une puissance de 2 > 0");
        return DSM_ERROR_INIT_ARG_PAGE_SIZE;
    }

    if (config->net.system.nb_nodes == 0) {
        LOG_ERROR("nb_nodes doit être > 0");
        return DSM_ERROR_INIT_ARG_NB_NODES;
    }

    if (config->net.system.nb_managers == 0 ||
        config->net.system.nb_managers >= config->net.system.nb_nodes) {
        LOG_ERROR("nb_managers doit être > 0 et < nb_nodes");
        return DSM_ERROR_INIT_ARG_NB_MANAGERS;
    }

    return 0;
}

static int dsm_ctx_init(const dsm_config *config) {
    if (dsm_ctx != NULL) {
        LOG_ERROR("DSM déjà initialisé");
        return DSM_ERROR_ALREADY_INIT;
    }

    int ret = validate_config(config);
    if (ret != 0) {
        return ret;
    }

    dsm_ctx = malloc(sizeof(dsm_context));
    if (!dsm_ctx) {
        LOG_ERROR("Erreur malloc : dsm_ctx");
        return DSM_ERROR_CONTEXT_ALLOC;
    }
    dsm_ctx->config = *config;
    dsm_ctx->node_id = 0;

    for (size_t i = 0; i < DSM_HASH_SIZE(dsm_ctx->nodes_reg); i++) {
        dsm_ctx->nodes_reg[i].first = NULL;
    }

    for (size_t i = 0; i < DSM_HASH_SIZE(dsm_ctx->pages_reg); i++) {
        dsm_ctx->pages_reg[i].first = NULL;
    }

    return 0;
}

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
    config->net.node.tcp_port = 0; // #accept
    config->net.node.udp_port = 0; // #accept
}

int dsm_init(const dsm_config *config) {
    LOG_INFO("Initialisation de DSM1SAR...");
    int ret = dsm_ctx_init(config);
    if (ret != 0) {
        return ret;
    }

    ret = dsm_network_init();

    // TODO SPLIT THREAD AVANT RETURN
    return ret;
}