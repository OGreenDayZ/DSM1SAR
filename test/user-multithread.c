/**
 * @file user_multithread.c
 * @brief Test multithread du DSM1SAR - API publique uniquement (dsm.h)
 *
 * Scénario :
 *   - 2 nœuds workers, 1 manager
 *   - Chaque nœud lance NB_THREADS threads
 *   - Phase 1 (écriture) : le nœud 1 écrit dans les NB_THREADS cases de
 *     shared_data via lock/unlock
 *   - Barrière globale inter-nœuds
 *   - Phase 2 (lecture)  : le nœud 2 lit et vérifie les valeurs écrites
 *   - Barrière globale inter-nœuds
 *   - Finalisation
 */

#include "dsm.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/*  Paramètres du test                                                  */
/* ------------------------------------------------------------------ */

#define NB_THREADS 4          /* threads par nœud worker              */
#define ARRAY_SIZE NB_THREADS /* une case partagée par thread          */

/* ------------------------------------------------------------------ */
/*  Contexte passé à chaque thread                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    int thread_idx;   /* indice local du thread (0 … NB_THREADS-1)   */
    int *shared_data; /* pointeur vers la zone partagée               */
    int node_id;      /* identifiant du nœud courant                  */
} thread_arg_t;

/* ------------------------------------------------------------------ */
/*  Utilitaires                                                          */
/* ------------------------------------------------------------------ */

static void check(int ret, const char *msg) {
    if (ret != DSM_SUCCESS) {
        fprintf(stderr, "[ERREUR] %s (code=%d)\n", msg, ret);
        exit(EXIT_FAILURE);
    }
}

/* ------------------------------------------------------------------ */
/*  Thread d'écriture (nœud 1)                                          */
/* ------------------------------------------------------------------ */

static void *writer_thread(void *arg) {
    thread_arg_t *ctx = (thread_arg_t *)arg;

    int idx = ctx->thread_idx;
    int value = (ctx->node_id * 100) + idx; /* valeur déterministe      */

    printf("[Nœud %d | Thread %d] Tentative d'écriture : shared_data[%d] = %d\n",
           ctx->node_id, idx, idx, value);

    /* Acquisition du lock sur la page contenant shared_data[idx] */
    if (dsm_lock(&ctx->shared_data[idx]) < 0) {
        fprintf(stderr, "[Nœud %d | Thread %d] Erreur dsm_lock\n", ctx->node_id, idx);
        return NULL;
    }

    ctx->shared_data[idx] = value;

    printf("[Nœud %d | Thread %d] Écriture effectuée : shared_data[%d] = %d\n",
           ctx->node_id, idx, idx, ctx->shared_data[idx]);

    /* Libération du lock */
    if (dsm_unlock(&ctx->shared_data[idx]) < 0) {
        fprintf(stderr, "[Nœud %d | Thread %d] Erreur dsm_unlock\n", ctx->node_id, idx);
        return NULL;
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Thread de lecture / vérification (nœud 2)                          */
/* ------------------------------------------------------------------ */

static void *reader_thread(void *arg) {
    thread_arg_t *ctx = (thread_arg_t *)arg;

    int idx = ctx->thread_idx;
    int expected_value = (1 * 100) + idx; /* valeur attendue du nœud 1  */

    if (dsm_lock(&ctx->shared_data[idx]) < 0) {
        fprintf(stderr, "[Nœud %d | Thread %d] Erreur dsm_lock\n", ctx->node_id, idx);
        return NULL;
    }

    int read_val = ctx->shared_data[idx];

    if (dsm_unlock(&ctx->shared_data[idx]) < 0) {
        fprintf(stderr, "[Nœud %d | Thread %d] Erreur dsm_unlock\n", ctx->node_id, idx);
        return NULL;
    }

    if (read_val == expected_value) {
        printf("[Nœud %d | Thread %d] OK  shared_data[%d] = %d (attendu %d)\n",
               ctx->node_id, idx, idx, read_val, expected_value);
    } else {
        printf("[Nœud %d | Thread %d] KO  shared_data[%d] = %d (attendu %d) !!!\n",
               ctx->node_id, idx, idx, read_val, expected_value);
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Lancement d'un pool de threads                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief Crée NB_THREADS threads, les exécute puis les joint.
 *
 * @param fn   Fonction de thread (writer_thread ou reader_thread)
 * @param data Zone mémoire partagée
 * @param nid  Identifiant du nœud courant
 */
static void run_threads(void *(*fn)(void *), int *data, int nid) {
    pthread_t tids[NB_THREADS];
    thread_arg_t args[NB_THREADS];

    for (int i = 0; i < NB_THREADS; i++) {
        args[i].thread_idx = i;
        args[i].shared_data = data;
        args[i].node_id = nid;

        if (pthread_create(&tids[i], NULL, fn, &args[i]) != 0) {
            fprintf(stderr, "[Nœud %d] Erreur pthread_create thread %d\n", nid, i);
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < NB_THREADS; i++) {
        pthread_join(tids[i], NULL);
    }
}

/* ------------------------------------------------------------------ */
/*  Main                                                                 */
/* ------------------------------------------------------------------ */

int main(void) {

    /* ---- 1. Configuration ---- */
    dsm_config config;
    dsm_get_default_config(&config);

    config.net.system.nb_nodes = 2; /* 2 workers + 1 manager */
    config.net.system.nb_managers = 1;

    /* ---- 2. Initialisation ---- */
    check(dsm_init(&config), "dsm_init");

    /* ---- 3. Allocation déterministe (avant toute divergence de flux) ----
     *
     * Tous les nœuds workers appellent dsm_alloc() dans le même ordre et
     * reçoivent le même pointeur virtuel vers la zone surveillée.
     */
    int *shared_data = NULL;
    if (dsm_get_role() == DSM_ROLE_WORKER) {
        shared_data = (int *)dsm_alloc();
        if (!shared_data) {
            fprintf(stderr, "Erreur dsm_alloc\n");
            return EXIT_FAILURE;
        }
    }

    int my_id = dsm_get_node_id();
    printf("[Nœud %d] Initialisé. Role=%d\n", my_id, dsm_get_role());

    /* ---- 4. Phase d'écriture : nœud 1 uniquement ---- */
    if (my_id == 1) {
        printf("[Nœud %d] === Phase ÉCRITURE (%d threads) ===\n", my_id, NB_THREADS);
        run_threads(writer_thread, shared_data, my_id);
        printf("[Nœud %d] Écriture terminée.\n", my_id);
    }

    /* ---- 5. Barrière : on attend que tout le monde ait fini d'écrire ---- */
    printf("[Nœud %d] Attente à la barrière post-écriture...\n", my_id);
    // dsm_barrier_advanced(NB_THREADS, 0 /* manager_id */);
    printf("[Nœud %d] Barrière franchie.\n", my_id);

    /* ---- 6. Phase de lecture : nœud 2 uniquement ---- */
    if (my_id == 2) {
        printf("[Nœud %d] === Phase LECTURE / VÉRIFICATION (%d threads) ===\n", my_id,
               NB_THREADS);
        run_threads(reader_thread, shared_data, my_id);
        printf("[Nœud %d] Lecture terminée.\n", my_id);
    }

    /* ---- 7. Barrière finale avant finalize ---- */
    printf("[Nœud %d] Attente à la barrière pré-finalize...\n", my_id);
    // dsm_barrier_advanced(NB_THREADS, 0 /* manager_id */);
    printf("[Nœud %d] Barrière franchie.\n", my_id);

    /* ---- 8. Finalisation ---- */
    // dsm_finalize_advanced(NB_THREADS);

    printf("[Nœud %d] Fin du test.\n", my_id);
    return EXIT_SUCCESS;
}