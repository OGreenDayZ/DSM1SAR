#include "debug/debug.h"
#include "dsm.h"
#include <string.h>
#include <unistd.h>

int main(int argc, char const *argv[]) {

    dsm_config config;
    dsm_get_default_config(&config);

    config.net.system.nb_nodes = 3;
    config.net.system.nb_managers = 1;

    config.net.node.role = DSM_ROLE_MANAGER; // Collision
    // dsm_init(&config);

    // // Test de dsm consécutif
    // if (argc > 1 && strcmp(argv[1], "1") == 0) {
    //     //
    // } else {
    //     LOG_INFO("Sleep 3 secondes...");
    //     // Sinon trop rapide (car redémarre en même temps) et ne se détecte pas à temps
    //     //  -> deviennent tous des superviseurs
    //     sleep(3);
    //     // config.net.system.system_id = 1; // Collision
    // }
    return dsm_init(&config);
}
