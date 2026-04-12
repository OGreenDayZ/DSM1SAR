#include "dsm.h"
#include <stdio.h>

int main(int argc, char const *argv[]) {

    dsm_config config;
    dsm_get_default_config(&config);

    config.net.system.nb_nodes = 5;
    config.net.system.nb_managers = 2;
    printf("j'ai bien compilé\n");
    return dsm_init(&config);
}
