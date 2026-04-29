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
    if (dsm_init(&config) != DSM_SUCCESS) {
        fprintf(stderr, "Erreur d'initialisation DSM\n");
        return EXIT_FAILURE;
    }

    // 2. Allocation déterministe
    // Tous les nœuds appellent dsm_alloc dans le même ordre.
    // Ils reçoivent TOUS le même pointeur virtuel vers la zone surveillée.
    int *shared_data = NULL;
    if (dsm_get_role() == DSM_ROLE_WORKER)
        shared_data = (int *)dsm_alloc();

    // On récupère l'ID du nœud (souvent stocké dans dsm_ctx)
    // Supposons que dsm_get_node_id() existe dans ton API
    int my_id = dsm_get_node_id();

    if (dsm_get_role() == DSM_ROLE_WORKER) {
        if (my_id == 1) {
            printf("[Nœud %d] J'initialise les données...\n", my_id);
            for (int i = 0; i < 100; i++) {
                // Ici, userfaultfd intercepte l'écriture
                LOG_INFO("Nœud %d écrit à l'index %d", my_id, i);
                printf("[Nœud %d] Ecriture de la donnée index %d : %d\n", my_id, i,
                       i * 2);
                shared_data[i] = i * 2;
            }
        } else {
            printf("[Noeud %d] Je veux afficher la valeur de la variable partagé \n",
                   my_id);
            printf("[Noeud %d] Je veux afficher la valeur de la variable partagé : %d\n",
                   my_id, shared_data[0]);
        }

        // 3. Synchronisation
        // On attend que le Nœud 0 ait fini d'écrire.
        // printf("[Nœud %d] Attente à la barrière...\n", my_id);
        // dsm_barrier();
        if (my_id != 1) {
            sleep(30);
            return 0;
        }
        if (my_id != 1) {
            // Ici, au moment de la lecture, une faute de page survient.
            // Le daemon attrape la faute, demande la page au Manager,
            // qui la récupère chez le Nœud 0.
            printf("[Nœud %d] Lecture de la donnée index 50 : %d\n", my_id,
                   shared_data[50]);
        }
    } else if (dsm_get_role() == DSM_ROLE_MANAGER) {
    }

    // 4. Nettoyage
    // dsm_finalize();
    sleep(30);
    return EXIT_SUCCESS;
}