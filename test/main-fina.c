#include "debug/debug.h"
#include "dsm.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// --- Couleurs pour l'affichage ---
#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN "\x1b[36m"
#define RESET "\x1b[0m"
#define BOLD "\033[1m"

// Structure pour passer des arguments aux threads de test
typedef struct {
    int thread_id;
    int *shared_mem;
} thread_args;

// --- Fonction exécutée par des threads secondaires sur chaque worker ---
void *worker_thread_logic(void *arg) {
    thread_args *t_args = (thread_args *)arg;
    int tid = t_args->thread_id;
    int *data = t_args->shared_mem;

    printf(YELLOW "[Thread %d] Arrivée à la barrière avancée...\n" RESET, tid);

    // Test de dsm_barrier_advanced : Attend que 2 threads locaux soient prêts
    // pour envoyer la requête de barrière au Manager 0.
    dsm_barrier_advanced(1, 0);

    printf(GREEN "[Thread %d] Sortie de la barrière avancée !\n" RESET, tid);

    // Test de verrouillage (Lock)
    printf(CYAN "[Thread %d] Tentative de verrouillage de la page...\n" RESET, tid);
    dsm_lock(&data[100]);
    printf(MAGENTA "[Thread %d] Verrou acquis, modification en cours...\n" RESET, tid);
    data[100] += (tid + 1);
    dsm_unlock(&data[100]);
    printf(CYAN "[Thread %d] Verrou relâché.\n" RESET, tid);

    // Finalisation avancée : Synchronise la fin de 2 threads locaux[cite: 3].
    dsm_finalize_advanced(2);
    return NULL;
}

int main() {
    dsm_config config;
    dsm_get_default_config(&config);

    // Configuration : 3 nœuds (1 manager, 2 workers)[cite: 1, 3].
    config.net.system.nb_nodes = 3;
    config.net.system.nb_managers = 1;

    printf(BOLD CYAN
           "\n[SYSTÈME] Initialisation du DSM (Mode de test avancé)...\n" RESET);

    dsm_node_role role = dsm_init(&config);
    if (role == -1) {
        fprintf(stderr, RED "Erreur d'initialisation DSM\n" RESET);
        return EXIT_FAILURE;
    }

    if (role == DSM_ROLE_MANAGER) {
        printf(BOLD MAGENTA
               "[MANAGER] En attente des requêtes de synchronisation...\n" RESET);
        // Le manager tourne en boucle jusqu'à ce que tous les workers finalisent[cite: 1,
        // 4].
        return EXIT_SUCCESS;
    }

    // --- LOGIQUE DU WORKER ---
    int node_id = dsm_get_node_id();
    printf(BOLD BLUE "[WORKER %d] Démarrage des tests.\n" RESET, node_id);

    // 1. Allocation de mémoire partagée[cite: 1, 3]
    int *shared_data = (int *)dsm_alloc();
    if (!shared_data) {
        return EXIT_FAILURE;
    }

    // 2. Test de Barrière Simple (tous les nœuds du système)[cite: 3]
    printf(YELLOW "[WORKER %d] Test dsm_barrier() global...\n" RESET, node_id);
    dsm_barrier();
    printf(GREEN "[WORKER %d] Barrière globale franchie.\n" RESET, node_id);

    // 3. Test de Barrière Avancée avec Threads Locaux
    // On crée un thread local pour tester la synchronisation multi-thread interne au
    // nœud.
    pthread_t extra_thread;
    thread_args t_args = {.thread_id = 1, .shared_mem = shared_data};

    if (pthread_create(&extra_thread, NULL, worker_thread_logic, &t_args) != 0) {
        perror("pthread_create");
        return EXIT_FAILURE;
    }

    // Le thread principal (Thread 0) participe aussi aux tests avancés
    printf(YELLOW "[Thread 0] Arrivée à la barrière avancée...\n" RESET);
    dsm_barrier_advanced(2, 0); // Attend le thread 1
    printf(GREEN "[Thread 0] Sortie de la barrière avancée !\n" RESET);

    // Accès mémoire concurrent protégé par lock
    dsm_lock(&shared_data[100]);
    printf(MAGENTA "[Thread 0] Écriture dans la zone verrouillée...\n" RESET);
    shared_data[100] = 1000 * node_id;
    dsm_unlock(&shared_data[100]);

    // 4. Finalisation avancée[cite: 3]
    printf(CYAN "[Thread 0] Tentative de dsm_finalize_advanced...\n" RESET);
    dsm_finalize_advanced(1);

    pthread_join(extra_thread, NULL);

    printf(BOLD GREEN
           "\n[SUCCÈS] Nœud %d a terminé tous les tests de synchronisation.\n" RESET,
           node_id);

    return EXIT_SUCCESS;
}