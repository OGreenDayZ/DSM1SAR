/**
 * @file test_drain_queue.c
 * @brief Tests unitaires pour dsm_drain_queue
 *
 * Compilation :
 *   gcc -Wall -Wextra -pthread -o test_drain_queue test_drain_queue.c -I.
 * Exécution :
 *   ./test_drain_queue
 */

#include "util/concurrent/drain_queue.h"

#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* =========================================================================
 * Infrastructure de test minimale
 * ====================================================================== */

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg)                                                           \
    do {                                                                                 \
        g_tests_run++;                                                                   \
        if (cond) {                                                                      \
            g_tests_passed++;                                                            \
            printf("  [PASS] %s\n", msg);                                                \
        } else {                                                                         \
            g_tests_failed++;                                                            \
            printf("  [FAIL] %s  (line %d)\n", msg, __LINE__);                           \
        }                                                                                \
    } while (0)

#define RUN_TEST(fn)                                                                     \
    do {                                                                                 \
        printf("\n=== %s ===\n", #fn);                                                   \
        fn();                                                                            \
    } while (0)

/* =========================================================================
 * Structure de données de test
 * ====================================================================== */

typedef struct {
    int value;
    dsm_dq_node node;
} item_t;

static item_t *make_item(int v) {
    item_t *it = malloc(sizeof(item_t));
    assert(it);
    it->value = v;
    it->node.next = NULL;
    return it;
}

static item_t *item_of(dsm_dq_node *n) {
    return (item_t *)((char *)n - offsetof(item_t, node));
}

/* =========================================================================
 * Tests unitaires séquentiels
 * ====================================================================== */

/* --- init / destroy ------------------------------------------------------- */

static void test_init_destroy(void) {
    dsm_drain_queue q;
    int ret = dsm_dq_init(&q);
    TEST_ASSERT(ret == 0, "dsm_dq_init retourne 0");
    TEST_ASSERT(q.head == NULL, "head == NULL après init");
    TEST_ASSERT(q.tail == NULL, "tail == NULL après init");
    dsm_dq_destroy(&q);
    TEST_ASSERT(1, "dsm_dq_destroy ne crashe pas");
}

static void test_destroy_null(void) {
    dsm_dq_destroy(NULL);
    TEST_ASSERT(1, "dsm_dq_destroy(NULL) ne crashe pas");
}

/* --- push ----------------------------------------------------------------- */

static void test_push_first_returns_1(void) {
    dsm_drain_queue q;
    dsm_dq_init(&q);

    item_t *a = make_item(1);
    int is_first = dsm_dq_push(&q, &a->node);
    TEST_ASSERT(is_first == 1, "push sur queue vide -> is_first == 1");

    dsm_dq_pop(&q);
    free(a);
    dsm_dq_destroy(&q);
}

static void test_push_second_returns_0(void) {
    dsm_drain_queue q;
    dsm_dq_init(&q);

    item_t *a = make_item(1);
    item_t *b = make_item(2);
    dsm_dq_push(&q, &a->node);
    int is_first = dsm_dq_push(&q, &b->node);
    TEST_ASSERT(is_first == 0, "push sur queue non-vide -> is_first == 0");

    dsm_dq_pop(&q);
    dsm_dq_pop(&q);
    free(a);
    free(b);
    dsm_dq_destroy(&q);
}

static void test_push_updates_head_tail(void) {
    dsm_drain_queue q;
    dsm_dq_init(&q);

    item_t *a = make_item(10);
    item_t *b = make_item(20);

    dsm_dq_push(&q, &a->node);
    TEST_ASSERT(q.head == &a->node, "head == a après premier push");
    TEST_ASSERT(q.tail == &a->node, "tail == a après premier push");

    dsm_dq_push(&q, &b->node);
    TEST_ASSERT(q.head == &a->node, "head reste a après second push");
    TEST_ASSERT(q.tail == &b->node, "tail == b après second push");

    dsm_dq_pop(&q);
    dsm_dq_pop(&q);
    free(a);
    free(b);
    dsm_dq_destroy(&q);
}

/* --- front ---------------------------------------------------------------- */

static void test_front_empty(void) {
    dsm_drain_queue q;
    dsm_dq_init(&q);
    TEST_ASSERT(dsm_dq_front(&q) == NULL, "front sur queue vide -> NULL");
    dsm_dq_destroy(&q);
}

static void test_front_non_empty(void) {
    dsm_drain_queue q;
    dsm_dq_init(&q);

    item_t *a = make_item(42);
    dsm_dq_push(&q, &a->node);
    TEST_ASSERT(dsm_dq_front(&q) == &a->node, "front retourne le premier nœud");

    dsm_dq_pop(&q);
    free(a);
    dsm_dq_destroy(&q);
}

static void test_front_does_not_remove(void) {
    dsm_drain_queue q;
    dsm_dq_init(&q);

    item_t *a = make_item(7);
    dsm_dq_push(&q, &a->node);
    dsm_dq_front(&q);
    TEST_ASSERT(q.head == &a->node, "front ne retire pas le nœud");

    dsm_dq_pop(&q);
    free(a);
    dsm_dq_destroy(&q);
}

/* --- pop ------------------------------------------------------------------ */

static void test_pop_last_returns_1(void) {
    dsm_drain_queue q;
    dsm_dq_init(&q);

    item_t *a = make_item(1);
    dsm_dq_push(&q, &a->node);
    int is_empty = dsm_dq_pop(&q);
    TEST_ASSERT(is_empty == 1, "pop du dernier élément -> is_empty == 1");
    TEST_ASSERT(q.head == NULL, "head == NULL après pop du dernier");
    TEST_ASSERT(q.tail == NULL, "tail == NULL après pop du dernier");

    free(a);
    dsm_dq_destroy(&q);
}

static void test_pop_not_last_returns_0(void) {
    dsm_drain_queue q;
    dsm_dq_init(&q);

    item_t *a = make_item(1);
    item_t *b = make_item(2);
    dsm_dq_push(&q, &a->node);
    dsm_dq_push(&q, &b->node);

    int is_empty = dsm_dq_pop(&q);
    TEST_ASSERT(is_empty == 0, "pop avec élément restant -> is_empty == 0");
    TEST_ASSERT(q.head == &b->node, "head == b après pop de a");

    dsm_dq_pop(&q);
    free(a);
    free(b);
    dsm_dq_destroy(&q);
}

/* --- ordre FIFO ----------------------------------------------------------- */

static void test_fifo_order(void) {
    dsm_drain_queue q;
    dsm_dq_init(&q);

    int n = 5;
    item_t *items[5];
    for (int i = 0; i < n; i++) {
        items[i] = make_item(i);
        dsm_dq_push(&q, &items[i]->node);
    }

    for (int i = 0; i < n; i++) {
        dsm_dq_node *front = dsm_dq_front(&q);
        TEST_ASSERT(front != NULL, "front non-NULL");
        TEST_ASSERT(item_of(front)->value == i, "ordre FIFO correct");
        dsm_dq_pop(&q);
        free(items[i]);
    }

    TEST_ASSERT(dsm_dq_front(&q) == NULL, "queue vide après drain complet");
    dsm_dq_destroy(&q);
}

/* --- réutilisation après drain complet ------------------------------------ */

static void test_reuse_after_drain(void) {
    dsm_drain_queue q;
    dsm_dq_init(&q);

    item_t *a = make_item(1);
    dsm_dq_push(&q, &a->node);
    dsm_dq_pop(&q);

    /* Après drain complet, un nouveau push doit à nouveau retourner 1 */
    item_t *b = make_item(2);
    int is_first = dsm_dq_push(&q, &b->node);
    TEST_ASSERT(is_first == 1, "push après drain complet -> is_first == 1");

    dsm_dq_pop(&q);
    free(a);
    free(b);
    dsm_dq_destroy(&q);
}

/* =========================================================================
 * Tests multi-thread
 * ====================================================================== */

#define NTHREADS 8
#define ITEMS_PER_THREAD 500

/*
 * Chaque thread pousse ses tâches. Si push retourne 1, il devient
 * drainer et traite toutes les tâches jusqu'à ce que la queue soit vide.
 */
typedef struct {
    dsm_drain_queue *q;
    int base_value;
    int processed; /* tâches traitées par ce thread */
} prod_arg_t;

static void *producer_drain(void *arg) {
    prod_arg_t *pa = arg;

    for (int i = 0; i < ITEMS_PER_THREAD; i++) {
        item_t *it = make_item(pa->base_value + i);

        if (dsm_dq_push(pa->q, &it->node)) {
            /* Drainer : vider la queue */
            dsm_dq_node *cur;
            while ((cur = dsm_dq_front(pa->q)) != NULL) {
                int is_empty = dsm_dq_pop(pa->q);
                free(item_of(cur));
                pa->processed++;
                if (is_empty)
                    break;
            }
        }
        /* sinon : tâche confiée, on continue */
    }
    return NULL;
}

static void test_concurrent_all_processed(void) {
    dsm_drain_queue q;
    dsm_dq_init(&q);

    pthread_t threads[NTHREADS];
    prod_arg_t args[NTHREADS];

    for (int i = 0; i < NTHREADS; i++) {
        args[i].q = &q;
        args[i].base_value = i * ITEMS_PER_THREAD;
        args[i].processed = 0;
        pthread_create(&threads[i], NULL, producer_drain, &args[i]);
    }

    int total = 0;
    for (int i = 0; i < NTHREADS; i++) {
        pthread_join(threads[i], NULL);
        total += args[i].processed;
    }

    TEST_ASSERT(total == NTHREADS * ITEMS_PER_THREAD,
                "toutes les tâches traitées exactement une fois");
    TEST_ASSERT(q.head == NULL, "queue vide après drain concurrent");
    TEST_ASSERT(q.tail == NULL, "tail == NULL après drain concurrent");

    dsm_dq_destroy(&q);
}

/*
 * Vérifie qu'il n'y a jamais deux drainers actifs simultanément.
 * On instrumente avec un compteur protégé par mutex.
 */
#define ND_THREADS 6
#define ND_ITEMS 300

typedef struct {
    dsm_drain_queue *q;
    int base;
    pthread_mutex_t *adl;
    int *active;
    int *max_active;
    int *violation;
} nd_arg_t;

static void *nd_worker(void *arg) {
    nd_arg_t *na = arg;

    for (int i = 0; i < ND_ITEMS; i++) {
        item_t *it = make_item(na->base + i);

        if (dsm_dq_push(na->q, &it->node)) {
            pthread_mutex_lock(na->adl);
            (*na->active)++;
            if (*na->active > *na->max_active)
                *na->max_active = *na->active;
            if (*na->active > 1)
                *na->violation = 1;
            pthread_mutex_unlock(na->adl);

            dsm_dq_node *cur;
            while ((cur = dsm_dq_front(na->q)) != NULL) {
                int empty = dsm_dq_pop(na->q);
                free(item_of(cur));
                if (empty)
                    break;
            }

            pthread_mutex_lock(na->adl);
            (*na->active)--;
            pthread_mutex_unlock(na->adl);
        }
    }
    return NULL;
}

static void test_no_double_drain(void) {
    dsm_drain_queue q;
    dsm_dq_init(&q);

    pthread_mutex_t adl = PTHREAD_MUTEX_INITIALIZER;
    int active = 0;
    int max_act = 0;
    int violation = 0;

    pthread_t threads[ND_THREADS];
    nd_arg_t args[ND_THREADS];

    for (int i = 0; i < ND_THREADS; i++) {
        args[i].q = &q;
        args[i].base = i * ND_ITEMS;
        args[i].adl = &adl;
        args[i].active = &active;
        args[i].max_active = &max_act;
        args[i].violation = &violation;
        pthread_create(&threads[i], NULL, nd_worker, &args[i]);
    }
    for (int i = 0; i < ND_THREADS; i++)
        pthread_join(threads[i], NULL);

    TEST_ASSERT(violation == 0, "jamais deux drainers actifs simultanément");
    TEST_ASSERT(max_act <= 1, "max drainers simultanés <= 1");
    TEST_ASSERT(q.head == NULL, "queue vide en fin de test");

    pthread_mutex_destroy(&adl);
    dsm_dq_destroy(&q);
}

/* =========================================================================
 * Main
 * ====================================================================== */

int main(void) {
    printf("======================================\n");
    printf("  Tests dsm_drain_queue\n");
    printf("======================================\n");

    RUN_TEST(test_init_destroy);
    RUN_TEST(test_destroy_null);

    RUN_TEST(test_push_first_returns_1);
    RUN_TEST(test_push_second_returns_0);
    RUN_TEST(test_push_updates_head_tail);

    RUN_TEST(test_front_empty);
    RUN_TEST(test_front_non_empty);
    RUN_TEST(test_front_does_not_remove);

    RUN_TEST(test_pop_last_returns_1);
    RUN_TEST(test_pop_not_last_returns_0);

    RUN_TEST(test_fifo_order);
    RUN_TEST(test_reuse_after_drain);

    RUN_TEST(test_concurrent_all_processed);
    RUN_TEST(test_no_double_drain);

    printf("\n======================================\n");
    printf("  Résultats : %d/%d passés", g_tests_passed, g_tests_run);
    if (g_tests_failed)
        printf("  (%d ÉCHEC(S))", g_tests_failed);
    printf("\n======================================\n");

    return g_tests_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}