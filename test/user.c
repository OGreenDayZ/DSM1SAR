#include "dsm.h"

#include "debug/debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {

    dsm_config config;
    dsm_get_default_config(&config);

    config.net.system.nb_nodes = 3;
    config.net.system.nb_managers = 1;

    // 1. Initialisation du DSM (crée le pager, le daemon, etc.)
    int ret = dsm_init(&config);
    if (ret < 0) {
        fprintf(stderr, "Erreur d'initialisation DSM\n");
        return EXIT_FAILURE;

    } else if (ret == DSM_ROLE_MANAGER) {
        return EXIT_SUCCESS;
    }

    // 2. Allocation déterministe
    // Tous les nœuds appellent dsm_alloc dans le même ordre.
    // Ils reçoivent TOUS le même pointeur virtuel vers la zone surveillée.
    int *shared_data = NULL;
    if (dsm_get_role() == DSM_ROLE_WORKER)
        shared_data = (int *)dsm_alloc();

    printf("Je vais lire les données avant initialisation \n");
    printf("%d", shared_data[0]);

    printf("J'initialise les données...\n");
    // Ici, userfaultfd intercepte l'écriture
    LOG_INFO("Nœud %d écrit à l'index %d", 50);
    printf(" Ecriture de la donnée index %d : %d\n", 50, 2 * 2);
    shared_data[50] = 2 * 2;

    printf("Lecture de la donnée index 50 : %d\n", shared_data[50]);
    dsm_finalize();
    return EXIT_SUCCESS;
}