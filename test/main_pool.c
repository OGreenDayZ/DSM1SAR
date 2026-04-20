/**
 * test_thread_pool.c
 * Suite de tests exhaustive pour dsm_thread_pool
 *
 * Compilation : gcc -o test_thread_pool test_thread_pool.c thread_pool.c -lpthread
 * Execution   : ./test_thread_pool
 */

#include "util/thread_pool.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * @author : claude
 */
/* ─────────────────────────── Helpers d'affichage ─────────────────────────── */

#define GREEN "\033[32m"
#define RED "\033[31m"
#define CYAN "\033[36m"
#define RESET "\033[0m"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                                       \
    do {                                                                                 \
        printf(CYAN "\n[TEST] %s\n" RESET, name);                                        \
    } while (0)

#define ASSERT(cond, msg)                                                                \
    do {                                                                                 \
        if (cond) {                                                                      \
            printf(GREEN "  ✓ %s\n" RESET, msg);                                         \
            tests_passed++;                                                              \
        } else {                                                                         \
            printf(RED "  ✗ %s  (line %d)\n" RESET, msg, __LINE__);                      \
            tests_failed++;                                                              \
        }                                                                                \
    } while (0)

/* ──────────────────────────── Tâches de test ─────────────────────────────── */

/* Incrémente un compteur atomique */
void task_increment(void *arg) {
    atomic_int *counter = (atomic_int *)arg;
    atomic_fetch_add(counter, 1);
}

/* Incrémente un compteur protégé par mutex */
typedef struct {
    pthread_mutex_t mu;
    int val;
} shared_counter_t;

void task_mutex_increment(void *arg) {
    shared_counter_t *c = (shared_counter_t *)arg;
    pthread_mutex_lock(&c->mu);
    c->val++;
    pthread_mutex_unlock(&c->mu);
}

/* Dors un peu (simule du travail) */
void task_sleep(void *arg) {
    int *ms = (int *)arg;
    usleep((*ms) * 1000);
}

/* Ecrit dans un tableau à l'indice donné (test d'ordre / corruption) */
typedef struct {
    int *array;
    int index;
} write_arg_t;

void task_write_index(void *arg) {
    write_arg_t *w = (write_arg_t *)arg;
    w->array[w->index] = w->index;
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  1. Création / Destruction basique
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_create_destroy(void) {
    TEST("Création et destruction basique");

    dsm_thread_pool *pool = pool_create(4, 16);
    ASSERT(pool != NULL, "pool_create retourne non-NULL");

    int ret = pool_destroy(pool, graceful_shutdown);
    ASSERT(ret == 0, "pool_destroy retourne 0");
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  2. Arguments invalides
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_invalid_args(void) {
    TEST("Arguments invalides");

    /* pool_add_task avec pool NULL */
    int ret = pool_add_task(NULL, task_increment, NULL);
    ASSERT(ret == -7, "add_task(NULL pool) → THREAD_INVALID_ARG");

    /* pool_add_task avec func NULL */
    dsm_thread_pool *pool = pool_create(2, 8);
    ret = pool_add_task(pool, NULL, NULL);
    ASSERT(ret == THREAD_INVALID_ARG, "add_task(NULL func) → THREAD_INVALID_ARG");
    pool_destroy(pool, brutal_shutdown);

    /* pool_free avec NULL */
    ret = pool_free(NULL);
    ASSERT(ret == THREAD_INVALID_ARG, "pool_free(NULL) → THREAD_INVALID_ARG");
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  3. Exécution de N tâches — vérification d'exactitude
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_task_execution(void) {
    TEST("Exécution correcte de 10000 tâches");

    const int N = 10000;
    atomic_int counter = 0;

    dsm_thread_pool *pool = pool_create(8, 128);
    ASSERT(pool != NULL, "Création du pool");

    int submitted = 0;
    while (submitted < N) {
        int ret = pool_add_task(pool, task_increment, &counter);
        if (ret == 0)
            submitted++;
        else if (ret == THREAD_FULL_QUEUE)
            usleep(100); /* queue pleine : on attend un peu */
        else {
            ASSERT(0, "add_task a retourné une erreur inattendue");
            break;
        }
    }

    pool_destroy(pool, graceful_shutdown);

    ASSERT(atomic_load(&counter) == N,
           "Compteur == 10000 après graceful_shutdown (toutes les tâches exécutées)");
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  4. Queue pleine
 * ══════════════════════════════════════════════════════════════════════════════ */
void task_block(void *arg) {
    /* Bloque jusqu'à ce que le flag soit mis à 1 */
    volatile int *flag = (volatile int *)arg;
    while (!(*flag))
        usleep(100);
}

void test_full_queue(void) {
    TEST("Queue pleine → THREAD_FULL_QUEUE");

    volatile int flag = 0;
    /* 1 thread, queue de 4 : on remplit avec des tâches bloquantes */
    dsm_thread_pool *pool = pool_create(1, 4);

    /* Le thread va prendre la 1ère tâche et bloquer → 3 slots libres */
    usleep(5000); /* laisse le thread démarrer */

    int full_seen = 0;
    for (int i = 0; i < 10; i++) {
        int ret = pool_add_task(pool, task_block, (void *)&flag);
        if (ret == THREAD_FULL_QUEUE) {
            full_seen = 1;
            break;
        }
        usleep(500);
    }
    ASSERT(full_seen, "THREAD_FULL_QUEUE observé quand queue saturée");

    flag = 1; /* débloque toutes les tâches */
    pool_destroy(pool, graceful_shutdown);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  5. Brutal shutdown — les tâches en attente sont abandonnées
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_brutal_shutdown(void) {
    TEST("Brutal shutdown (tâches en attente abandonnées)");

    volatile int flag = 0;
    atomic_int counter = 0;

    dsm_thread_pool *pool = pool_create(2, 32);

    /* Remplie la queue avec des tâches bloquantes */
    for (int i = 0; i < 4; i++)
        pool_add_task(pool, task_block, (void *)&flag);

    /* Puis des tâches d'incrément qui ne doivent PAS toutes passer */
    for (int i = 0; i < 20; i++)
        pool_add_task(pool, task_increment, &counter);

    flag = 1; /* inutile en brutal, mais évite le UB si un thread tourne encore */
    int ret = pool_destroy(pool, brutal_shutdown);

    ASSERT(ret == 0, "pool_destroy brutal retourne 0");
    ASSERT(atomic_load(&counter) < 20,
           "Compteur < 20 (certaines tâches abandonnées par brutal shutdown)");
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  6. Graceful shutdown — toutes les tâches déjà en queue sont exécutées
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_graceful_shutdown(void) {
    TEST("Graceful shutdown (toutes les tâches en queue exécutées)");

    atomic_int counter = 0;
    dsm_thread_pool *pool = pool_create(2, 64);

    for (int i = 0; i < 50; i++)
        pool_add_task(pool, task_increment, &counter);

    pool_destroy(pool, graceful_shutdown);

    ASSERT(atomic_load(&counter) == 50,
           "Compteur == 50 (graceful : toutes les tâches traitées)");
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  7. add_task après shutdown → THREAD_SHUTING_DOWN
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Tâche qui demande le shutdown depuis l'intérieur */
typedef struct {
    dsm_thread_pool *pool;
    atomic_int *counter;
} shutdown_arg_t;

void test_add_after_shutdown(void) {
    TEST("add_task refusée après shutdown");

    dsm_thread_pool *pool = pool_create(2, 8);
    pool_destroy(pool, graceful_shutdown);

    /*
     * pool pointe vers de la mémoire libérée ; on ne peut plus
     * lire pool->shutdown proprement. Ce test vérifie surtout
     * que la vérification de shutdown dans add_task se fait
     * AVANT le lock, donc sans déréférencement dangereux.
     *
     * Pour tester proprement, on utilise un pool "zombifié"
     * en settant shutdown manuellement avant destroy.
     */
    dsm_thread_pool *pool2 = pool_create(1, 4);
    /* On force shutdown à 1 sans détruire le pool */
    pool2->shutdown = brutal_shutdown;

    atomic_int counter = 0;
    int ret = pool_add_task(pool2, task_increment, &counter);
    ASSERT(ret == THREAD_SHUTING_DOWN,
           "add_task retourne THREAD_SHUTING_DOWN si pool->shutdown != 0");

    /* Cleanup : remettre shutdown à 0 pour que pool_destroy fonctionne */
    pool2->shutdown = 0;
    pool_destroy(pool2, brutal_shutdown);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  8. Thread-safety — écritures concurrentes sans corruption
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_concurrent_writes(void) {
    TEST("Thread-safety : 500 écritures concurrentes dans un tableau");

    const int N = 500;
    int *array = calloc(N, sizeof(int));
    write_arg_t *args = malloc(N * sizeof(write_arg_t));

    dsm_thread_pool *pool = pool_create(8, 128);

    int submitted = 0;
    while (submitted < N) {
        args[submitted].array = array;
        args[submitted].index = submitted;
        int ret = pool_add_task(pool, task_write_index, &args[submitted]);
        if (ret == 0)
            submitted++;
        else
            usleep(100);
    }

    pool_destroy(pool, graceful_shutdown);

    int ok = 1;
    for (int i = 0; i < N; i++) {
        if (array[i] != i) {
            ok = 0;
            break;
        }
    }
    ASSERT(ok, "Chaque case du tableau vaut son indice (pas de corruption)");

    free(array);
    free(args);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  9. Stress test — beaucoup de threads, beaucoup de tâches
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_stress(void) {
    TEST("Stress : 16 threads, 5000 tâches");

    atomic_int counter = 0;
    const int N = 5000;

    dsm_thread_pool *pool = pool_create(16, 256);

    int submitted = 0;
    while (submitted < N) {
        int ret = pool_add_task(pool, task_increment, &counter);
        if (ret == 0)
            submitted++;
        else if (ret == THREAD_FULL_QUEUE)
            usleep(50);
    }

    pool_destroy(pool, graceful_shutdown);
    ASSERT(atomic_load(&counter) == N, "Stress : exactement 5000 tâches exécutées");
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  10. Pool à 1 thread
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_single_thread(void) {
    TEST("Pool à 1 thread — séquentialité garantie");

    atomic_int counter = 0;
    const int N = 100;

    dsm_thread_pool *pool = pool_create(1, 64);

    for (int i = 0; i < N; i++) {
        while (pool_add_task(pool, task_increment, &counter) == THREAD_FULL_QUEUE)
            usleep(100);
    }

    pool_destroy(pool, graceful_shutdown);
    ASSERT(atomic_load(&counter) == N, "1 thread : compteur == N");
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  11. Pool à queue de taille 1
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_queue_size_one(void) {
    TEST("Queue de taille 1");

    atomic_int counter = 0;
    const int N = 50;

    dsm_thread_pool *pool = pool_create(2, 1);
    int submitted = 0;
    while (submitted < N) {
        int ret = pool_add_task(pool, task_increment, &counter);
        if (ret == 0)
            submitted++;
        else if (ret == THREAD_FULL_QUEUE)
            usleep(200);
    }
    pool_destroy(pool, graceful_shutdown);
    ASSERT(atomic_load(&counter) == N, "Queue=1 : toutes les tâches exécutées");
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  12. Plusieurs créations/destructions successives (pas de fuite de ressources)
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_repeated_create_destroy(void) {
    TEST("10 créations/destructions successives (pas de crash)");

    int ok = 1;
    for (int i = 0; i < 10; i++) {
        dsm_thread_pool *pool = pool_create(4, 16);
        if (!pool) {
            ok = 0;
            break;
        }
        atomic_int counter = 0;
        for (int j = 0; j < 20; j++)
            while (pool_add_task(pool, task_increment, &counter) == THREAD_FULL_QUEUE)
                usleep(50);
        pool_destroy(pool, graceful_shutdown);
    }
    ASSERT(ok, "Aucun crash sur 10 cycles create/destroy");
}
/* ══════════════════════════════════════════════════════════════════════════════
 *  PROBLÈME DES LECTEURS / ÉCRIVAINS
 *
 *  Implémentation maison d'un verrou Lecteurs-Écrivains (priorité lecteurs)
 *  pour valider que le thread pool ne brise pas les invariants de concurrence.
 *
 *  Invariants à vérifier :
 *    I1 — Jamais deux écrivains simultanés            (exclusion mutuelle)
 *    I2 — Jamais un écrivain et un lecteur simultanés (exclusion mixte)
 *    I3 — Plusieurs lecteurs simultanés sont autorisés
 *    I4 — Les écrivains progressent (pas de famine)
 * ══════════════════════════════════════════════════════════════════════════════ */

/* ── Verrou lecteurs-écrivains ─────────────────────────────────────────────── */

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t can_read;
    pthread_cond_t can_write;
    int readers_active;  /* lecteurs en section critique */
    int readers_waiting; /* lecteurs en attente */
    int writers_active;  /* écrivain en section critique (0 ou 1) */
    int writers_waiting; /* écrivains en attente */
} rw_lock_t;

static void rw_init(rw_lock_t *rw) {
    pthread_mutex_init(&rw->mutex, NULL);
    pthread_cond_init(&rw->can_read, NULL);
    pthread_cond_init(&rw->can_write, NULL);
    rw->readers_active = 0;
    rw->readers_waiting = 0;
    rw->writers_active = 0;
    rw->writers_waiting = 0;
}

static void rw_destroy(rw_lock_t *rw) {
    pthread_mutex_destroy(&rw->mutex);
    pthread_cond_destroy(&rw->can_read);
    pthread_cond_destroy(&rw->can_write);
}

/* Priorité aux lecteurs : un lecteur passe si aucun écrivain n'est actif */
static void rw_read_lock(rw_lock_t *rw) {
    pthread_mutex_lock(&rw->mutex);
    rw->readers_waiting++;
    while (rw->writers_active > 0)
        pthread_cond_wait(&rw->can_read, &rw->mutex);
    rw->readers_waiting--;
    rw->readers_active++;
    pthread_mutex_unlock(&rw->mutex);
}

static void rw_read_unlock(rw_lock_t *rw) {
    pthread_mutex_lock(&rw->mutex);
    rw->readers_active--;
    if (rw->readers_active == 0 && rw->writers_waiting > 0)
        pthread_cond_signal(&rw->can_write);
    pthread_mutex_unlock(&rw->mutex);
}

/* Un écrivain passe si aucun lecteur ni écrivain n'est actif */
static void rw_write_lock(rw_lock_t *rw) {
    pthread_mutex_lock(&rw->mutex);
    rw->writers_waiting++;
    while (rw->readers_active > 0 || rw->writers_active > 0)
        pthread_cond_wait(&rw->can_write, &rw->mutex);
    rw->writers_waiting--;
    rw->writers_active++;
    pthread_mutex_unlock(&rw->mutex);
}

static void rw_write_unlock(rw_lock_t *rw) {
    pthread_mutex_lock(&rw->mutex);
    rw->writers_active--;
    /* Réveille d'abord les éventuels lecteurs, sinon un écrivain */
    if (rw->readers_waiting > 0)
        pthread_cond_broadcast(&rw->can_read);
    else if (rw->writers_waiting > 0)
        pthread_cond_signal(&rw->can_write);
    pthread_mutex_unlock(&rw->mutex);
}

/* ── Ressource partagée surveillée ────────────────────────────────────────── */

typedef struct {
    rw_lock_t lock;
    int data; /* valeur protégée */

    /* Compteurs d'activité pour détecter les violations */
    atomic_int concurrent_writers;    /* doit rester ≤ 1 */
    atomic_int writers_while_reading; /* doit rester == 0 */
    atomic_int max_concurrent_readers;

    /* Compteurs de travail */
    atomic_int reads_done;
    atomic_int writes_done;

    /* Violations détectées */
    atomic_int violation_writer_writer; /* I1 */
    atomic_int violation_writer_reader; /* I2 */

    /* Suivi lecteurs actifs (pour vérifier I3) */
    atomic_int readers_now;
    atomic_int peak_readers;
} shared_resource_t;

static void resource_init(shared_resource_t *r) {
    rw_init(&r->lock);
    r->data = 0;
    atomic_init(&r->concurrent_writers, 0);
    atomic_init(&r->writers_while_reading, 0);
    atomic_init(&r->max_concurrent_readers, 0);
    atomic_init(&r->reads_done, 0);
    atomic_init(&r->writes_done, 0);
    atomic_init(&r->violation_writer_writer, 0);
    atomic_init(&r->violation_writer_reader, 0);
    atomic_init(&r->readers_now, 0);
    atomic_init(&r->peak_readers, 0);
}

static void resource_destroy(shared_resource_t *r) {
    rw_destroy(&r->lock);
}

/* ── Arguments des tâches lecteurs/écrivains ──────────────────────────────── */

typedef struct {
    shared_resource_t *res;
    int iterations;
} rw_task_arg_t;

/* Tâche lecteur */
void task_reader(void *arg) {
    rw_task_arg_t *a = (rw_task_arg_t *)arg;
    shared_resource_t *res = a->res;

    for (int i = 0; i < a->iterations; i++) {
        rw_read_lock(&res->lock);

        /* ── Entrée en section critique ── */
        int rn = atomic_fetch_add(&res->readers_now, 1) + 1;

        /* Mettre à jour le pic de lecteurs simultanés */
        int peak = atomic_load(&res->peak_readers);
        while (rn > peak && !atomic_compare_exchange_weak(&res->peak_readers, &peak, rn))
            ; /* CAS loop */

        /* Vérifier I2 : aucun écrivain ne doit être actif */
        if (atomic_load(&res->concurrent_writers) > 0)
            atomic_fetch_add(&res->violation_writer_reader, 1);

        /* Simule un accès en lecture */
        usleep(10);
        (void)res->data; /* lecture effective */

        atomic_fetch_sub(&res->readers_now, 1);
        /* ── Sortie de section critique ── */

        rw_read_unlock(&res->lock);
        atomic_fetch_add(&res->reads_done, 1);
    }
}

/* Tâche écrivain */
void task_writer(void *arg) {
    rw_task_arg_t *a = (rw_task_arg_t *)arg;
    shared_resource_t *res = a->res;

    for (int i = 0; i < a->iterations; i++) {
        rw_write_lock(&res->lock);

        /* ── Entrée en section critique ── */
        int wc = atomic_fetch_add(&res->concurrent_writers, 1) + 1;

        /* Vérifier I1 : jamais deux écrivains simultanés */
        if (wc > 1)
            atomic_fetch_add(&res->violation_writer_writer, 1);

        /* Vérifier I2 : aucun lecteur ne doit être actif */
        if (atomic_load(&res->readers_now) > 0)
            atomic_fetch_add(&res->violation_writer_reader, 1);

        /* Simule une écriture */
        usleep(20);
        res->data++;

        atomic_fetch_sub(&res->concurrent_writers, 1);
        /* ── Sortie de section critique ── */

        rw_write_unlock(&res->lock);
        atomic_fetch_add(&res->writes_done, 1);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  13. Exclusion mutuelle entre écrivains (I1)
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_rw_writer_exclusion(void) {
    TEST("Lecteurs/Écrivains — I1 : exclusion mutuelle entre écrivains");

    shared_resource_t res;
    resource_init(&res);

    const int NW = 8, ITER = 50;
    rw_task_arg_t args[NW];
    dsm_thread_pool *pool = pool_create(8, 64);

    for (int i = 0; i < NW; i++) {
        args[i].res = &res;
        args[i].iterations = ITER;
        while (pool_add_task(pool, task_writer, &args[i]) == THREAD_FULL_QUEUE)
            usleep(100);
    }
    pool_destroy(pool, graceful_shutdown);

    ASSERT(atomic_load(&res.violation_writer_writer) == 0,
           "I1 : jamais deux écrivains simultanés");
    ASSERT(atomic_load(&res.writes_done) == NW * ITER,
           "I1 : toutes les écritures effectuées");
    ASSERT(res.data == NW * ITER, "I1 : valeur finale de data == nb total d'écritures");

    resource_destroy(&res);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  14. Exclusion écrivain/lecteur (I2)
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_rw_mixed_exclusion(void) {
    TEST("Lecteurs/Écrivains — I2 : exclusion écrivain ↔ lecteur");

    shared_resource_t res;
    resource_init(&res);

    const int NW = 4, NR = 8, ITER = 30;
    rw_task_arg_t wargs[NW], rargs[NR];
    dsm_thread_pool *pool = pool_create(12, 128);

    /* On entrelace soumissions lecteurs et écrivains */
    for (int i = 0; i < NW || i < NR; i++) {
        if (i < NW) {
            wargs[i].res = &res;
            wargs[i].iterations = ITER;
            while (pool_add_task(pool, task_writer, &wargs[i]) == THREAD_FULL_QUEUE)
                usleep(50);
        }
        if (i < NR) {
            rargs[i].res = &res;
            rargs[i].iterations = ITER;
            while (pool_add_task(pool, task_reader, &rargs[i]) == THREAD_FULL_QUEUE)
                usleep(50);
        }
    }
    pool_destroy(pool, graceful_shutdown);

    ASSERT(atomic_load(&res.violation_writer_reader) == 0,
           "I2 : jamais écrivain et lecteur simultanés");
    ASSERT(atomic_load(&res.writes_done) == NW * ITER,
           "I2 : toutes les écritures effectuées");
    ASSERT(atomic_load(&res.reads_done) == NR * ITER,
           "I2 : toutes les lectures effectuées");

    resource_destroy(&res);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  15. Lectures simultanées autorisées (I3)
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_rw_concurrent_readers(void) {
    TEST("Lecteurs/Écrivains — I3 : plusieurs lecteurs simultanés autorisés");

    shared_resource_t res;
    resource_init(&res);

    const int NR = 16, ITER = 5;
    rw_task_arg_t rargs[NR];
    dsm_thread_pool *pool = pool_create(16, 64);

    for (int i = 0; i < NR; i++) {
        rargs[i].res = &res;
        rargs[i].iterations = ITER;
        while (pool_add_task(pool, task_reader, &rargs[i]) == THREAD_FULL_QUEUE)
            usleep(50);
    }
    pool_destroy(pool, graceful_shutdown);

    ASSERT(atomic_load(&res.violation_writer_reader) == 0,
           "I3 : aucune violation pendant lectures concurrentes");
    ASSERT(atomic_load(&res.peak_readers) > 1,
           "I3 : pic > 1 lecteur simultané observé (parallélisme réel)");
    ASSERT(atomic_load(&res.reads_done) == NR * ITER,
           "I3 : toutes les lectures effectuées");

    printf("       (pic observé : %d lecteurs simultanés)\n",
           atomic_load(&res.peak_readers));

    resource_destroy(&res);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  16. Famine des écrivains — les écrivains progressent (I4)
 *
 *  Scénario : flux constant de lecteurs + quelques écrivains.
 *  Si les écrivains n'obtiennent jamais le verrou → writes_done < NW*ITER.
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_rw_writer_progress(void) {
    TEST("Lecteurs/Écrivains — I4 : les écrivains progressent (pas de famine)");

    shared_resource_t res;
    resource_init(&res);

    const int NW = 4, NR = 12, ITER_W = 20, ITER_R = 10;
    rw_task_arg_t wargs[NW], rargs[NR];
    dsm_thread_pool *pool = pool_create(16, 256);

    /* Soumettre d'abord les lecteurs en masse, puis les écrivains */
    for (int i = 0; i < NR; i++) {
        rargs[i].res = &res;
        rargs[i].iterations = ITER_R;
        while (pool_add_task(pool, task_reader, &rargs[i]) == THREAD_FULL_QUEUE)
            usleep(50);
    }
    for (int i = 0; i < NW; i++) {
        wargs[i].res = &res;
        wargs[i].iterations = ITER_W;
        while (pool_add_task(pool, task_writer, &wargs[i]) == THREAD_FULL_QUEUE)
            usleep(50);
    }

    pool_destroy(pool, graceful_shutdown);

    ASSERT(atomic_load(&res.writes_done) == NW * ITER_W,
           "I4 : tous les écrivains ont progressé (pas de famine)");
    ASSERT(atomic_load(&res.violation_writer_writer) == 0,
           "I4 : aucune violation I1 pendant le test de famine");
    ASSERT(atomic_load(&res.violation_writer_reader) == 0,
           "I4 : aucune violation I2 pendant le test de famine");

    resource_destroy(&res);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  17. Stress lecteurs/écrivains — tous invariants sous forte charge
 * ══════════════════════════════════════════════════════════════════════════════ */
void test_rw_stress(void) {
    TEST("Lecteurs/Écrivains — Stress : tous invariants sous forte charge");

    shared_resource_t res;
    resource_init(&res);

    const int NW = 8, NR = 24, ITER = 40;
    rw_task_arg_t wargs[NW], rargs[NR];
    dsm_thread_pool *pool = pool_create(16, 512);

    for (int i = 0; i < NW; i++) {
        wargs[i].res = &res;
        wargs[i].iterations = ITER;
        while (pool_add_task(pool, task_writer, &wargs[i]) == THREAD_FULL_QUEUE)
            usleep(30);
    }
    for (int i = 0; i < NR; i++) {
        rargs[i].res = &res;
        rargs[i].iterations = ITER;
        while (pool_add_task(pool, task_reader, &rargs[i]) == THREAD_FULL_QUEUE)
            usleep(30);
    }

    pool_destroy(pool, graceful_shutdown);

    ASSERT(atomic_load(&res.violation_writer_writer) == 0,
           "Stress I1 : exclusion écrivains");
    ASSERT(atomic_load(&res.violation_writer_reader) == 0, "Stress I2 : exclusion mixte");
    ASSERT(atomic_load(&res.writes_done) == NW * ITER,
           "Stress : toutes les écritures effectuées");
    ASSERT(atomic_load(&res.reads_done) == NR * ITER,
           "Stress : toutes les lectures effectuées");
    ASSERT(res.data == NW * ITER,
           "Stress : valeur finale cohérente (pas de lost update)");

    printf("       (pic de lecteurs simultanés : %d)\n", atomic_load(&res.peak_readers));

    resource_destroy(&res);
}

/* ══════════════════════════════════════════════════════════════════════════════
 *  Main
 * ══════════════════════════════════════════════════════════════════════════════ */
int main(void) {
    printf(CYAN "═══════════════════════════════════════════\n");
    printf("   Tests exhaustifs : dsm_thread_pool\n");
    printf("═══════════════════════════════════════════\n" RESET);

    printf("Nombre de coeurs logique : %ld\n", DSM_DEFAULT_NB_THREADS);
    test_create_destroy();
    test_invalid_args();
    test_task_execution();
    test_full_queue();
    test_brutal_shutdown();
    test_graceful_shutdown();
    test_add_after_shutdown();
    test_concurrent_writes();
    test_stress();
    test_single_thread();
    test_queue_size_one();
    test_repeated_create_destroy();

    printf(CYAN "\n── Lecteurs / Écrivains ───────────────────\n" RESET);
    test_rw_writer_exclusion();
    test_rw_mixed_exclusion();
    test_rw_concurrent_readers();
    test_rw_writer_progress();
    test_rw_stress();

    printf(CYAN "\n═══════════════════════════════════════════\n" RESET);
    printf("Résultats : " GREEN "%d passés" RESET " / " RED "%d échoués" RESET
           " / %d total\n",
           tests_passed, tests_failed, tests_passed + tests_failed);

    return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}