#include "debug/debug.h"
#include "dsm.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// --- Définition des couleurs pour un affichage plus clair ---
#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN "\x1b[36m"
#define RESET "\x1b[0m"
#define BOLD "\033[1m"

int main() {
    dsm_config config;
    dsm_get_default_config(&config);

    config.net.system.nb_nodes = 4;
    config.net.system.nb_managers = 2;

    printf(BOLD CYAN "\n[SYSTÈME] Initialisation du DSM...\n" RESET);

    // 1. Initialisation du DSM
    int ret = dsm_init(&config);
    if (ret < 0) {
        fprintf(stderr, RED "Erreur d'initialisation DSM\n" RESET);
        return EXIT_FAILURE;

    } else if (ret == DSM_ROLE_MANAGER) {
        printf(BOLD MAGENTA
               "[MANAGER] Le manager est prêt et attend les connexions.\n" RESET);
        return EXIT_SUCCESS;
    }

    // 2. Allocation déterministe
    int *shared_data = NULL;
    printf(BOLD YELLOW
           "[WORKER] Tentative d'allocation de la mémoire partagée...\n" RESET);
    shared_data = (int *)dsm_alloc();
    printf(GREEN "[OK] Mémoire allouée à l'adresse : %p\n" RESET, (void *)shared_data);

    // --- Phase de Lecture (Page Fault attendu) ---
    printf(BLUE "\n--- Phase de LECTURE ---\n" RESET);
    printf("Je vais lire les données avant initialisation (Ceci devrait déclencher un "
           "Page Fault lecture)\n");
    printf("Valeur lue à l'index 0 : " BOLD "%d\n", shared_data[0]);

    // --- Phase d'Écriture ---
    printf(BLUE "\n--- Phase d'ÉCRITURE ---\n" RESET);
    printf(YELLOW "J'initialise les données...\n" RESET);

    // Ici, userfaultfd intercepte l'écriture
    LOG_INFO("Nœud %d écrit à l'index %d", 50);

    int value_to_write = 2 * 2;
    printf(CYAN "Action : Écriture de la valeur " BOLD "%d" RESET CYAN " à l'index " BOLD
                "50" RESET "\n",
           value_to_write);
    shared_data[50] = value_to_write;

    // --- Vérification finale ---
    printf(GREEN "\n[SUCCÈS] Lecture de la donnée index 50 après écriture : " BOLD
                 "%d\n" RESET,
           shared_data[50]);

    printf(BOLD CYAN "\n[SYSTÈME] Finalisation du DSM et nettoyage...\n" RESET);
    dsm_finalize();
    return EXIT_SUCCESS;
}