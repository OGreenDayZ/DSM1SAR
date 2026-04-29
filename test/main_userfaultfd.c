#include "dsm.h"
#include "internal.h"

#include "debug/debug.h"
#include "dsm.h"
#include <string.h>
#include <unistd.h>

int main(int argc, char const *argv[]) {

    dsm_config config;
    dsm_get_default_config(&config);

    config.net.system.nb_nodes = 3;
    config.net.system.nb_managers = 1;

    // config.net.node.role = DSM_ROLE_MANAGER; // Collision
    dsm_init(&config);

    printf("%d\n", EFFECTIVE_PAGESIZE);
    return 0;
}