#include "dsm.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// --- Couleurs pour les logs ---
#define CLR_RESET "\x1b[0m"
#define CLR_TITLE "\x1b[1;35m" // Magenta Gras
#define CLR_STEP "\x1b[1;36m"  // Cyan Gras
#define CLR_INFO "\x1b[32m"    // Vert
#define CLR_WARN "\x1b[33m"    // Jaune
#define CLR_ERR "\x1b[1;31m"   // Rouge Gras

#define TEST_LOG(color, msg, ...)                                                        \
    printf(color "[TEST] " msg CLR_RESET "\n", ##__VA_ARGS__)

// Structure pour les threads de test
typedef struct {
    void *addr;
    int id;
} thread_data_t;

// --- Scénarios de test ---

// 1. Lecture simple (Déclenche READ_REQ)
void *test_read_task(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    TEST_LOG(CLR_INFO, "Thread %d: Tentative de LECTURE à %p", data->id, data->addr);

    int val = *(int *)data->addr; // Déclenche le handler UFFD (READ)

    TEST_LOG(CLR_INFO, "Thread %d: LECTURE réussie ! Valeur lue = %d", data->id, val);
    return NULL;
}

// 2. Écriture simple (Déclenche WRITE_REQ ou Promotion)
void *test_write_task(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    int new_val = 100 + data->id;
    TEST_LOG(CLR_WARN, "Thread %d: Tentative d'ÉCRITURE de %d à %p", data->id, new_val,
             data->addr);

    *(int *)data->addr = new_val; // Déclenche le handler UFFD (WRITE)

    TEST_LOG(CLR_WARN, "Thread %d: ÉCRITURE terminée avec succès", data->id);
    return NULL;
}

// 3. Test de Verrou (LOCK_REQ / UNLOCK_REQ)
void *test_lock_task(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    TEST_LOG(CLR_STEP, "Thread %d: Demande de dsm_lock() sur %p", data->id, data->addr);

    if (dsm_lock(data->addr) == 0) {
        TEST_LOG(CLR_STEP, "Thread %d: Verrou ACQUIS. Section critique...", data->id);
        sleep(1); // Simulation de travail
        dsm_unlock(data->addr);
        TEST_LOG(CLR_STEP, "Thread %d: Verrou LIBÉRÉ (dsm_unlock)", data->id);
    } else {
        TEST_LOG(CLR_ERR, "Thread %d: Échec du lock !", data->id);
    }
    return NULL;
}

void *test_concurrent_write_task(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;

    // On simule plusieurs écritures rapides pour forcer les changements d'états
    for (int i = 0; i < 5; i++) {
        int val = (data->id * 100) + i;

        TEST_LOG(CLR_ERR,
                 "Thread %d: [CONCURRENCE] Tentative d'écriture n°%d (Valeur: %d)",
                 data->id, i, val);

        // Cette ligne va potentiellement déclencher :
        // 1. Un passage de NONE -> WRITE_SENT dans requests_reg[cite: 1, 3]
        // 2. Un message WRITE_REQ au manager
        // 3. Une attente sur entry->mutex si un autre thread écrit déjà
        *(int *)data->addr = val;

        TEST_LOG(CLR_INFO, "Thread %d: [CONCURRENCE] Écriture n°%d réussie", data->id, i);

        // Petit sleep pour laisser une chance aux autres threads d'interrompre
        usleep(100000);
    }

    return NULL;
}
int main(int argc, char *argv[]) {
    printf(CLR_TITLE "=============================================\n");
    printf("   Banc de Test DSM : Tous les Cas d'Usage   \n");
    printf("=============================================\n" CLR_RESET);

    // 1. Initialisation de la configuration
    dsm_config config;
    dsm_get_default_config(&config);

    config.net.system.nb_nodes = 3;
    config.net.system.nb_managers = 1;
    config.net.node.role = DSM_ROLE_WORKER;

    TEST_LOG(CLR_STEP, "Étape 1: Initialisation du contexte DSM...");
    if (dsm_init(&config) < 0) {
        TEST_LOG(CLR_ERR, "Erreur fatale : dsm_init a échoué.");
        return EXIT_FAILURE;
    }

    // 2. Allocation de mémoire DSM
    TEST_LOG(CLR_STEP, "Étape 2: Allocation d'une page partagée...");
    int *shared_mem = (int *)dsm_alloc();
    if (!shared_mem) {
        TEST_LOG(CLR_ERR, "Erreur fatale : dsm_alloc a échoué.");
        return EXIT_FAILURE;
    }
    TEST_LOG(CLR_INFO, "Page allouée à l'adresse virtuelle : %p", (void *)shared_mem);

    pthread_t threads[6];
    thread_data_t tdata[6];

    // --- CAS A : Première Lecture (Page Vide / EMPTY_INIT) ---
    printf("\n" CLR_TITLE "--- CAS A : PREMIÈRE LECTURE (MISSING PAGE) ---" CLR_RESET
           "\n");
    tdata[0] = (thread_data_t){shared_mem, 0};
    pthread_create(&threads[0], NULL, test_read_task, &tdata[0]);
    pthread_join(threads[0], NULL);

    // --- CAS B : Écriture sur page en Read-Only (PROMOTION / WRITE_REQ) ---
    printf("\n" CLR_TITLE "--- CAS B : ÉCRITURE (PROMOTION DE DROITS) ---" CLR_RESET
           "\n");
    tdata[1] = (thread_data_t){shared_mem, 1};
    pthread_create(&threads[1], NULL, test_write_task, &tdata[1]);
    pthread_join(threads[1], NULL);

    // --- CAS C : Accès Concurrents Multi-Threads (READ/READ) ---
    printf("\n" CLR_TITLE
           "--- CAS C : LECTURES CONCURRENTES (MÉCANISME PASSAGER) ---" CLR_RESET "\n");
    for (int i = 2; i < 4; i++) {
        tdata[i] = (thread_data_t){shared_mem, i};
        pthread_create(&threads[i], NULL, test_read_task, &tdata[i]);
    }
    for (int i = 2; i < 4; i++)
        pthread_join(threads[i], NULL);

    // --- CAS E : ÉCRITURES CONCURRENTES (RACE CONDITION &INVALIDATIONS) ---
    // printf("\n" CLR_TITLE "--- CAS E : ÉCRITURES CONCURRENTES (RACE CONDITION & "
    //        "INVALIDATIONS) ---" CLR_RESET "\n");
    // TEST_LOG(CLR_WARN,
    //          "Lancement de plusieurs threads écrivant sur la MÊME page
    //          simultanément...");

    // for (int i = 0; i < 4; i++) {
    //     tdata[i].addr = shared_mem;
    //     tdata[i].id = i + 10; // IDs distincts pour les logs
    //     pthread_create(&threads[i], NULL, test_concurrent_write_task, &tdata[i]);
    // }

    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    TEST_LOG(CLR_STEP, "Vérification finale : la valeur en mémoire est %d",
             *(int *)shared_mem);
    // --- CAS D : Verrous Exclusifs (LOCK / UNLOCK) ---
    printf("\n" CLR_TITLE "--- CAS D : TEST DES VERROUS (DSM_LOCK) ---" CLR_RESET "\n");
    for (int i = 4; i < 6; i++) {
        tdata[i] = (thread_data_t){shared_mem, i};
        pthread_create(&threads[i], NULL, test_lock_task, &tdata[i]);
    }
    for (int i = 4; i < 6; i++)
        pthread_join(threads[i], NULL);

    // 3. Nettoyage (Si implémenté)
    printf("\n" CLR_STEP "Étape Final: Fin des tests." CLR_RESET "\n");
    // dsm_ctx_cleanup(); // Si exposé

    return EXIT_SUCCESS;
}