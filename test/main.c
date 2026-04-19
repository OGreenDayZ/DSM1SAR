#include "dsm.h"
#include <stdio.h>

int main(int argc, char const *argv[]) {

    dsm_config config;
    dsm_get_default_config(&config);

    config.net.system.nb_nodes = 3;
    config.net.system.nb_managers = 1;

    config.net.node.role = DSM_ROLE_MANAGER; // Collision
    return dsm_init(&config);
}
