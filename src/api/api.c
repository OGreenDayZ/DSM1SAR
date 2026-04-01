#include "dsm.h"

#include <string.h>

#include "network/network.h"
#include "pager/pager.h"

void dsm_get_default_config(dsm_config_t *config) {
    // Pour certains champs, si 0 gardé => soulève erreur à l'init (ex:nb_nodes)
    config->pager.nb_pages = DSM_NB_PAGES;

    config->net.system.system_id = 0;
    config->net.system.nb_managers = 0;
    config->net.system.nb_nodes = 0;
    snprintf(config->net.system.multicast_ip, INET_ADDRSTRLEN, "%s", DSM_MULTICAST_IP);
    config->net.system.multicast_port = DSM_MULTICAST_PORT;

    config->net.node.role = DSM_ROLE_UNDEFINED;
    config->net.node.tcp_port = 0;
    config->net.node.udp_port = 0;
}

int dsm_init(const dsm_config_t *config) {
    return DSM_ERROR_SYSTEM_FULL;
}