#include "debug/debug.h"
#include "dsm.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char const *argv[]) {

    dsm_config config;
    dsm_get_default_config(&config);

    config.net.system.nb_nodes = 5;
    config.net.system.nb_managers = 2;

    config.net.node.role = DSM_ROLE_WORKER; // Collision
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

    int ret = dsm_init(&config);
    if (ret < 0) {
        fprintf(stderr, "Erreur d'initialisation DSM\n");
        return -1;

    } else if (ret == DSM_ROLE_MANAGER) {
        return 0;
    }

    int *shared_data = (int *)dsm_alloc();
    if (shared_data == NULL) {
        fprintf(stderr, "Erreur d'allocation DSM\n");
        return -1;
    }

    printf("Je vais lire les données avant initialisation \n");
    printf("%d\n", shared_data[0]);

    printf("J'initialise les données...\n");
    // Ici, userfaultfd intercepte l'écriture
    shared_data[50]++;

    printf("Lecture de la donnée : %d\n", shared_data[50]);

    dsm_finalize();

    return 0;
}
