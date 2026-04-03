#include "dsm.h"

#include <stdio.h>
#include <stdlib.h>

#include "debug/debug.h"
#include "internal.h"
#include "network/network.h"
#include "pager/pager.h"

dsm_context_t *dsm_ctx = NULL;

static int dsm_validate_config(const dsm_config_t *config) {
    if (config->pager.nb_pages == 0) {
        LOG_INFO("nb_pages doit être > 0");
        return DSM_ERROR_INIT_ARG_NB_PAGES;
    }

    if (config->net.system.nb_nodes == 0) {
        LOG_INFO("nb_nodes doit être > 0");
        return DSM_ERROR_INIT_ARG_NB_NODES;
    }

    if (config->net.system.nb_managers == 0 ||
        config->net.system.nb_managers >= config->net.system.nb_nodes) {
        LOG_INFO("nb_managers doit être > 0 et < nb_nodes");
        return DSM_ERROR_INIT_ARG_NB_MANAGERS;
    }

    return 0;
}

static int dsm_context_init(const dsm_config_t *config) {
    if (dsm_ctx != NULL) {
        return DSM_ERROR_ALREADY_INIT;
    }

    int ret = dsm_validate_config(config);
    if (ret != 0) {
        return ret;
    }

    dsm_ctx = malloc(sizeof(dsm_config_t));
    if (!dsm_ctx) {
        return DSM_ERROR_CONTEXT_ALLOC;
    }
    dsm_ctx->config = *config;
    return 0;
}

void dsm_get_default_config(dsm_config_t *config) {
    // Pour certains champs, si 0 gardé => soulève erreur à l'init (ex:nb_nodes)
    // et #accept si la valeur sera redéfinie en interne si non spécifiée par l'user
    config->pager.nb_pages = DSM_NB_PAGES;

    config->net.system.system_id = 0;
    config->net.system.nb_managers = 0;
    config->net.system.nb_nodes = 0;
    snprintf(config->net.system.multicast_ip, INET_ADDRSTRLEN, "%s", DSM_MULTICAST_IP);
    config->net.system.multicast_port = DSM_MULTICAST_PORT;

    config->net.node.role = DSM_ROLE_UNDEFINED; // #accept
    config->net.node.tcp_port = 0;              // #accept
    config->net.node.udp_port = 0;              // #accept
}

int dsm_init(const dsm_config_t *config) {
    LOG_INFO("Initialisation de DSM1SAR...");
    int ret = dsm_context_init(config);
    if (ret != 0) {
        return ret;
    }

    ret = dsm_network_init();
    return 0;
}