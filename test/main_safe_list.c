/**
 * @file test_dsm_safe_list.c
 * @brief Tests unitaires et de concurrence pour dsm_safe_list
 *
 * Compilation :
 *   gcc -Wall -Wextra -O2 -pthread \
 *       -I. test_dsm_safe_list.c list.c safe_list.c -o test_dsm_safe_list
 *
 * Avec ThreadSanitizer :
 *   gcc -fsanitize=thread -g -pthread \
 *       -I. test_dsm_safe_list.c list.c safe_list.c -o test_tsan && ./test_tsan
 *
 * Avec Helgrind :
 *   valgrind --tool=helgrind ./test_dsm_safe_list
 */

#include "util/concurrent/safe_list.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

/* =========================================================================
 * Infrastructure de test
 * ========================================================================= */

static int g_run = 0;
static int g_passed = 0;
static int g_failed = 0;

#define TEST_BEGIN(name)                                                                 \
    do {                                                                                 \
        g_run++;                                                                         \
        printf("  %-58s ", "[" name "]");                                                \
        fflush(stdout);                                                                  \
    } while (0)

#define TEST_PASS()                                                                      \
    do {                                                                                 \
        g_passed++;                                                                      \
        printf("OK\n");                                                                  \
    } while (0)

#define TEST_FAIL(msg)                                                                   \
    do {                                                                                 \
        g_failed++;                                                                      \
        printf("FAIL — %s (ligne %d)\n", (msg), __LINE__);                               \
        return;                                                                          \
    } while (0)

/* EXPECT hors boucle for_each — return direct si échec */
#define EXPECT(cond, msg)                                                                \
    do {                                                                                 \
        if (!(cond))                                                                     \
            TEST_FAIL(msg);                                                              \
    } while (0)

/*
 * EXPECT_UNLOCK : à utiliser UNIQUEMENT à l'intérieur d'un for_each ou
 * for_each_safe. Déverrouille la liste avant de retourner pour éviter de
 * fuiter le lock en cas d'échec (un return depuis le corps de la boucle
 * ne passe pas par la condition de sortie qui appelle unlock).
 */
#define EXPECT_UNLOCK(lst, cond, msg)                                                    \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            dsm_safe_list_unlock(lst);                                                   \
            g_failed++;                                                                  \
            printf("FAIL — %s (ligne %d)\n", (msg), __LINE__);                           \
            return;                                                                      \
        }                                                                                \
    } while (0)

/* =========================================================================
 * Structure de test
 * ========================================================================= */

struct item {
    int val;
    dsm_list_head node;
};

static struct item *make_item(int val) {
    struct item *it = malloc(sizeof(*it));
    assert(it);
    it->val = val;
    it->node.next = NULL;
    it->node.prev = NULL;
    return it;
}

/* Vide et libère tous les éléments via for_each_safe. */
static void drain(dsm_safe_list *lst) {
    struct item *pos, *n;
    dsm_safe_list_for_each_entry_safe(lst, pos, n, node) {
        dsm_list_del(&pos->node);
        free(pos);
    }
}

/* =========================================================================
 * Suite 1 – Fonctionnel (mono-thread)
 * ========================================================================= */

static void test_init_empty(void) {
    TEST_BEGIN("init : liste vide après init");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init retourne 0");
    EXPECT(dsm_safe_list_empty(&lst), "empty == 1");
    EXPECT(dsm_safe_list_count(&lst) == 0, "count == 0");
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

static void test_add_head_order(void) {
    TEST_BEGIN("add  : ordre LIFO (add en tête)");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    struct item *a = make_item(1);
    struct item *b = make_item(2);
    struct item *c = make_item(3);
    dsm_safe_list_add(&lst, &a->node);
    dsm_safe_list_add(&lst, &b->node);
    dsm_safe_list_add(&lst, &c->node);

    EXPECT(dsm_safe_list_count(&lst) == 3, "count == 3");

    /*
     * Accumulation des résultats hors EXPECT à l'intérieur de la boucle :
     * un return depuis le corps ne passerait pas par le unlock de la macro.
     */
    int expected[] = {3, 2, 1};
    int i = 0, order_ok = 1;
    struct item *pos;
    dsm_safe_list_for_each_entry(&lst, pos, node) {
        if (i >= 3 || pos->val != expected[i])
            order_ok = 0;
        i++;
    }
    EXPECT(order_ok, "ordre LIFO incorrect");
    EXPECT(i == 3, "exactement 3 éléments visités");

    drain(&lst);
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

static void test_add_tail_order(void) {
    TEST_BEGIN("add  : ordre FIFO (add_tail)");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    for (int i = 1; i <= 4; i++)
        dsm_safe_list_add_tail(&lst, &make_item(i)->node);

    EXPECT(dsm_safe_list_count(&lst) == 4, "count == 4");

    int expected = 1, order_ok = 1, visited = 0;
    struct item *pos;
    dsm_safe_list_for_each_entry(&lst, pos, node) {
        if (pos->val != expected)
            order_ok = 0;
        expected++;
        visited++;
    }
    EXPECT(order_ok, "ordre FIFO incorrect");
    EXPECT(visited == 4, "exactement 4 éléments visités");

    drain(&lst);
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

static void test_del_middle(void) {
    TEST_BEGIN("del  : suppression d'un élément médian");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    struct item *a = make_item(1);
    struct item *b = make_item(2);
    struct item *c = make_item(3);
    dsm_safe_list_add_tail(&lst, &a->node);
    dsm_safe_list_add_tail(&lst, &b->node);
    dsm_safe_list_add_tail(&lst, &c->node);

    dsm_safe_list_del(&lst, &b->node);
    free(b);

    EXPECT(dsm_safe_list_count(&lst) == 2, "count == 2 après del");

    int expected[] = {1, 3};
    int i = 0, order_ok = 1;
    struct item *pos;
    dsm_safe_list_for_each_entry(&lst, pos, node) {
        if (i >= 2 || pos->val != expected[i])
            order_ok = 0;
        i++;
    }
    EXPECT(order_ok, "ordre préservé après del médian");
    EXPECT(i == 2, "2 éléments visités");

    drain(&lst);
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

static void test_del_head(void) {
    TEST_BEGIN("del  : suppression de la tête");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    struct item *a = make_item(10);
    struct item *b = make_item(20);
    dsm_safe_list_add_tail(&lst, &a->node);
    dsm_safe_list_add_tail(&lst, &b->node);

    dsm_safe_list_del(&lst, &a->node);
    free(a);

    EXPECT(dsm_safe_list_count(&lst) == 1, "count == 1");
    EXPECT(!dsm_safe_list_empty(&lst), "non vide");

    int found_val = -1;
    struct item *pos;
    dsm_safe_list_for_each_entry(&lst, pos, node) {
        found_val = pos->val;
    }
    EXPECT(found_val == 20, "seul b(20) reste");

    drain(&lst);
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

static void test_del_tail(void) {
    TEST_BEGIN("del  : suppression de la queue");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    struct item *a = make_item(10);
    struct item *b = make_item(20);
    dsm_safe_list_add_tail(&lst, &a->node);
    dsm_safe_list_add_tail(&lst, &b->node);

    dsm_safe_list_del(&lst, &b->node);
    free(b);

    EXPECT(dsm_safe_list_count(&lst) == 1, "count == 1");

    int found_val = -1;
    struct item *pos;
    dsm_safe_list_for_each_entry(&lst, pos, node) {
        found_val = pos->val;
    }
    EXPECT(found_val == 10, "seul a(10) reste");

    drain(&lst);
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

static void test_del_last(void) {
    TEST_BEGIN("del  : suppression du dernier → liste vide");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    struct item *a = make_item(99);
    dsm_safe_list_add(&lst, &a->node);
    dsm_safe_list_del(&lst, &a->node);
    free(a);

    EXPECT(dsm_safe_list_empty(&lst), "vide après del");
    EXPECT(dsm_safe_list_count(&lst) == 0, "count == 0");

    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

static void test_del_poison(void) {
    TEST_BEGIN("del  : next/prev == NULL (poison) après suppression");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    struct item *a = make_item(1);
    dsm_safe_list_add(&lst, &a->node);
    dsm_safe_list_del(&lst, &a->node);

    EXPECT(a->node.next == NULL, "next == NULL (poison)");
    EXPECT(a->node.prev == NULL, "prev == NULL (poison)");

    free(a);
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

static void test_for_each_safe_delete(void) {
    TEST_BEGIN("iter : for_each_safe + suppression des pairs");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    for (int i = 1; i <= 10; i++)
        dsm_safe_list_add_tail(&lst, &make_item(i)->node);

    struct item *pos, *n;
    dsm_safe_list_for_each_entry_safe(&lst, pos, n, node) {
        if (pos->val % 2 == 0) {
            dsm_list_del(&pos->node);
            free(pos);
        }
    }

    EXPECT(dsm_safe_list_count(&lst) == 5, "5 éléments impairs restants");

    int expected[] = {1, 3, 5, 7, 9};
    int i = 0, order_ok = 1;
    dsm_safe_list_for_each_entry(&lst, pos, node) {
        if (i >= 5 || pos->val != expected[i])
            order_ok = 0;
        i++;
    }
    EXPECT(order_ok, "ordre des impairs préservé");
    EXPECT(i == 5, "exactement 5 impairs visités");

    drain(&lst);
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

static void test_for_each_empty(void) {
    TEST_BEGIN("iter : for_each sur liste vide");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    int visited = 0;
    struct item *pos;
    dsm_safe_list_for_each_entry(&lst, pos, node) {
        visited++;
    }
    EXPECT(visited == 0, "aucune itération sur liste vide");

    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

static void test_count_empty_coherence(void) {
    TEST_BEGIN("util : cohérence count/empty (add → del)");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    EXPECT(dsm_safe_list_empty(&lst), "vide au départ");
    EXPECT(dsm_safe_list_count(&lst) == 0, "count 0 au départ");

    struct item *a = make_item(1);
    struct item *b = make_item(2);

    dsm_safe_list_add(&lst, &a->node);
    EXPECT(!dsm_safe_list_empty(&lst), "non vide après add");
    EXPECT(dsm_safe_list_count(&lst) == 1, "count 1 après add");

    dsm_safe_list_add(&lst, &b->node);
    EXPECT(dsm_safe_list_count(&lst) == 2, "count 2 après second add");

    dsm_safe_list_del(&lst, &a->node);
    free(a);
    EXPECT(dsm_safe_list_count(&lst) == 1, "count 1 après premier del");

    dsm_safe_list_del(&lst, &b->node);
    free(b);
    EXPECT(dsm_safe_list_empty(&lst), "vide après second del");
    EXPECT(dsm_safe_list_count(&lst) == 0, "count 0 après second del");

    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

/* =========================================================================
 * Suite 2 – Concurrence
 * ========================================================================= */

#define N_THREADS 8
#define N_PER_THR 512

/* 2.1 — insertions concurrentes */

typedef struct {
    dsm_safe_list *lst;
    int thread_id;
    int n;
    struct item **inserted;
} writer_arg_t;

static void *writer_thread(void *arg) {
    writer_arg_t *wa = arg;
    wa->inserted = malloc((size_t)wa->n * sizeof(*wa->inserted));
    assert(wa->inserted);
    for (int i = 0; i < wa->n; i++) {
        wa->inserted[i] = make_item(wa->thread_id * wa->n + i);
        dsm_safe_list_add(wa->lst, &wa->inserted[i]->node);
    }
    return NULL;
}

static void test_concurrent_add(void) {
    TEST_BEGIN("conc : insertions concurrentes (8 × 512)");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    pthread_t tids[N_THREADS];
    writer_arg_t args[N_THREADS];
    for (int t = 0; t < N_THREADS; t++) {
        args[t].lst = &lst;
        args[t].thread_id = t;
        args[t].n = N_PER_THR;
        args[t].inserted = NULL;
        pthread_create(&tids[t], NULL, writer_thread, &args[t]);
    }
    for (int t = 0; t < N_THREADS; t++)
        pthread_join(tids[t], NULL);

    EXPECT(dsm_safe_list_count(&lst) == (size_t)(N_THREADS * N_PER_THR),
           "count == N_THREADS × N_PER_THR");

    for (int t = 0; t < N_THREADS; t++) {
        for (int i = 0; i < N_PER_THR; i++) {
            dsm_safe_list_del(&lst, &args[t].inserted[i]->node);
            free(args[t].inserted[i]);
        }
        free(args[t].inserted);
    }
    EXPECT(dsm_safe_list_empty(&lst), "vide après nettoyage");
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

/* 2.2 — add et add_tail simultanés */

typedef struct {
    dsm_safe_list *lst;
    int n;
    int use_tail;
} adder_arg_t;

static void *adder_thread(void *arg) {
    adder_arg_t *a = arg;
    for (int i = 0; i < a->n; i++) {
        struct item *it = make_item(i);
        if (a->use_tail)
            dsm_safe_list_add_tail(a->lst, &it->node);
        else
            dsm_safe_list_add(a->lst, &it->node);
    }
    return NULL;
}

static void test_concurrent_add_mix(void) {
    TEST_BEGIN("conc : add et add_tail simultanés");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    pthread_t tids[N_THREADS];
    adder_arg_t args[N_THREADS];
    for (int t = 0; t < N_THREADS; t++) {
        args[t].lst = &lst;
        args[t].n = N_PER_THR;
        args[t].use_tail = t % 2;
        pthread_create(&tids[t], NULL, adder_thread, &args[t]);
    }
    for (int t = 0; t < N_THREADS; t++)
        pthread_join(tids[t], NULL);

    EXPECT(dsm_safe_list_count(&lst) == (size_t)(N_THREADS * N_PER_THR),
           "count correct après mix add/add_tail");

    drain(&lst);
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

/* 2.3 — lecteurs concurrents */

typedef struct {
    dsm_safe_list *lst;
    int n_lookups;
    atomic_int *total_visited;
} reader_arg_t;

static void *reader_thread(void *arg) {
    reader_arg_t *ra = arg;
    for (int i = 0; i < ra->n_lookups; i++) {
        int local = 0;
        struct item *pos;
        dsm_safe_list_for_each_entry(ra->lst, pos, node) {
            local++;
            (void)pos->val;
        }
        atomic_fetch_add(ra->total_visited, local);
    }
    return NULL;
}

static void test_concurrent_reads(void) {
    TEST_BEGIN("conc : lectures concurrentes (8 readers)");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    int n_items = 64;
    for (int i = 0; i < n_items; i++)
        dsm_safe_list_add_tail(&lst, &make_item(i)->node);

    atomic_int total_visited = 0;
    int n_lookups = 200;

    pthread_t tids[N_THREADS];
    reader_arg_t rargs[N_THREADS];
    for (int t = 0; t < N_THREADS; t++) {
        rargs[t].lst = &lst;
        rargs[t].n_lookups = n_lookups;
        rargs[t].total_visited = &total_visited;
        pthread_create(&tids[t], NULL, reader_thread, &rargs[t]);
    }
    for (int t = 0; t < N_THREADS; t++)
        pthread_join(tids[t], NULL);

    EXPECT(atomic_load(&total_visited) == N_THREADS * n_lookups * n_items,
           "total visites == threads × lookups × éléments");

    drain(&lst);
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

/* 2.4 — suppressions concurrentes */

typedef struct {
    dsm_safe_list *lst;
    struct item **items;
    int start;
    int count;
} deleter_arg_t;

static void *deleter_thread(void *arg) {
    deleter_arg_t *da = arg;
    for (int i = da->start; i < da->start + da->count; i++) {
        dsm_safe_list_del(da->lst, &da->items[i]->node);
        free(da->items[i]);
        da->items[i] = NULL;
    }
    return NULL;
}

static void test_concurrent_del(void) {
    TEST_BEGIN("conc : suppressions concurrentes");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    int total = N_THREADS * N_PER_THR;
    struct item **items = malloc((size_t)total * sizeof(*items));
    assert(items);

    for (int i = 0; i < total; i++) {
        items[i] = make_item(i);
        dsm_safe_list_add_tail(&lst, &items[i]->node);
    }
    EXPECT(dsm_safe_list_count(&lst) == (size_t)total, "count initial correct");

    pthread_t tids[N_THREADS];
    deleter_arg_t dargs[N_THREADS];
    for (int t = 0; t < N_THREADS; t++) {
        dargs[t].lst = &lst;
        dargs[t].items = items;
        dargs[t].start = t * N_PER_THR;
        dargs[t].count = N_PER_THR;
        pthread_create(&tids[t], NULL, deleter_thread, &dargs[t]);
    }
    for (int t = 0; t < N_THREADS; t++)
        pthread_join(tids[t], NULL);

    EXPECT(dsm_safe_list_empty(&lst), "liste vide après suppressions concurrentes");

    free(items);
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

/* 2.5 — lecteurs + écrivains simultanés */

typedef struct {
    dsm_safe_list *lst;
    int n;
    int thread_id;
} mixed_arg_t;

static void *mixed_writer(void *arg) {
    mixed_arg_t *a = arg;
    for (int i = 0; i < a->n; i++) {
        struct item *it = make_item(a->thread_id * a->n + i);
        dsm_safe_list_add(a->lst, &it->node);
    }
    return NULL;
}

static void *mixed_reader(void *arg) {
    mixed_arg_t *a = arg;
    volatile size_t dummy = 0;
    struct item *pos;
    for (int i = 0; i < a->n; i++) {
        dsm_safe_list_for_each_entry(a->lst, pos, node) {
            dummy += (size_t)pos->val;
        }
    }
    (void)dummy;
    return NULL;
}

static void test_concurrent_mixed(void) {
    TEST_BEGIN("conc : lecteurs + écrivains simultanés");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    pthread_t tids[N_THREADS];
    mixed_arg_t args[N_THREADS];
    for (int t = 0; t < N_THREADS; t++) {
        args[t].lst = &lst;
        args[t].n = 200;
        args[t].thread_id = t;
        if (t < N_THREADS / 2)
            pthread_create(&tids[t], NULL, mixed_writer, &args[t]);
        else
            pthread_create(&tids[t], NULL, mixed_reader, &args[t]);
    }
    for (int t = 0; t < N_THREADS; t++)
        pthread_join(tids[t], NULL);

    EXPECT(dsm_safe_list_count(&lst) == (size_t)(N_THREADS / 2 * 200),
           "count == writers × n après test mixte");

    drain(&lst);
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

/* 2.6 — stress add → del intercalés */

typedef struct {
    dsm_safe_list *lst;
    int n;
} stress_arg_t;

static void *stress_thread(void *arg) {
    stress_arg_t *sa = arg;
    for (int i = 0; i < sa->n; i++) {
        struct item *it = make_item(i);
        dsm_safe_list_add(sa->lst, &it->node);
        dsm_safe_list_del(sa->lst, &it->node);
        free(it);
    }
    return NULL;
}

static void test_stress_add_del(void) {
    TEST_BEGIN("conc : stress add+del intercalés");
    dsm_safe_list lst;
    EXPECT(dsm_safe_list_init(&lst) == 0, "init");

    pthread_t tids[N_THREADS];
    stress_arg_t args[N_THREADS];
    for (int t = 0; t < N_THREADS; t++) {
        args[t].lst = &lst;
        args[t].n = N_PER_THR;
        pthread_create(&tids[t], NULL, stress_thread, &args[t]);
    }
    for (int t = 0; t < N_THREADS; t++)
        pthread_join(tids[t], NULL);

    EXPECT(dsm_safe_list_empty(&lst), "liste vide : chaque add a été suivi d'un del");
    dsm_safe_list_destroy(&lst);
    TEST_PASS();
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void) {
    printf("\n========================================================\n");
    printf("  Tests dsm_safe_list\n");
    printf("========================================================\n\n");

    printf("Suite 1 – Fonctionnel (mono-thread)\n");
    printf("--------------------------------------------------------\n");
    test_init_empty();
    test_add_head_order();
    test_add_tail_order();
    test_del_middle();
    test_del_head();
    test_del_tail();
    test_del_last();
    test_del_poison();
    test_for_each_safe_delete();
    test_for_each_empty();
    test_count_empty_coherence();

    printf("\nSuite 2 – Concurrence (%d threads)\n", N_THREADS);
    printf("--------------------------------------------------------\n");
    test_concurrent_add();
    test_concurrent_add_mix();
    test_concurrent_reads();
    test_concurrent_del();
    test_concurrent_mixed();
    test_stress_add_del();

    printf("\n========================================================\n");
    printf("  Résultat : %d/%d tests passés", g_passed, g_run);
    if (g_failed)
        printf("  (%d ÉCHEC(S))", g_failed);
    printf("\n========================================================\n\n");

    return g_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}