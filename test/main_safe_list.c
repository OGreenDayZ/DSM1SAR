/**
 * @file test_safe_list.c
 * @brief Tests unitaires pour dsm_safe_list
 *
 * Compilation :
 *   gcc -Wall -Wextra -pthread -o test_safe_list test_safe_list.c safe_list.c list.c -I.
 * Exécution :
 *   ./test_safe_list
 */

#include "util/concurrent/safe_list.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Infrastructure de test minimale
 * ---------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------
 * Structure de données embarquant un noeud de liste
 * ---------------------------------------------------------------------- */

typedef struct {
    int value;
    dsm_list_head node;
} item_t;

static item_t *make_item(int v) {
    item_t *it = malloc(sizeof(item_t));
    assert(it);
    it->value = v;
    dsm_list_init(&it->node);
    return it;
}

static void free_all(dsm_safe_list *list) {
    item_t *pos, *n;
    dsm_safe_list_for_each_entry_safe(list, pos, n, node) {
        dsm_list_del(&pos->node);
        free(pos);
    }
}

/* =========================================================================
 * Tests
 * ====================================================================== */

/* --- init / destroy ------------------------------------------------------- */

static void test_init_destroy(void) {
    dsm_safe_list list;
    int ret = dsm_safe_list_init(&list);
    TEST_ASSERT(ret == 0, "dsm_safe_list_init retourne 0");
    TEST_ASSERT(dsm_safe_list_empty(&list), "liste vide après init");
    TEST_ASSERT(dsm_safe_list_count(&list) == 0, "count == 0 après init");
    dsm_safe_list_destroy(&list);
    TEST_ASSERT(1, "dsm_safe_list_destroy ne crashe pas");
}

static void test_destroy_null(void) {
    /* dsm_safe_list_destroy(NULL) ne doit pas crasher */
    dsm_safe_list_destroy(NULL);
    TEST_ASSERT(1, "dsm_safe_list_destroy(NULL) ne crashe pas");
}

/* --- add / add_tail ------------------------------------------------------- */

static void test_add_one_element(void) {
    dsm_safe_list list;
    dsm_safe_list_init(&list);

    item_t *it = make_item(42);
    dsm_safe_list_add(&list, &it->node);

    TEST_ASSERT(!dsm_safe_list_empty(&list), "liste non-vide après add");
    TEST_ASSERT(dsm_safe_list_count(&list) == 1, "count == 1 après add");

    free_all(&list);
    dsm_safe_list_destroy(&list);
}

static void test_add_fifo_order(void) {
    /* dsm_list_add insère en tête (LIFO).
     * On vérifie l'ordre obtenu lors du parcours. */
    dsm_safe_list list;
    dsm_safe_list_init(&list);

    item_t *a = make_item(1);
    item_t *b = make_item(2);
    item_t *c = make_item(3);
    dsm_safe_list_add(&list, &a->node); /* tête: [1]       */
    dsm_safe_list_add(&list, &b->node); /* tête: [2,1]     */
    dsm_safe_list_add(&list, &c->node); /* tête: [3,2,1]   */

    TEST_ASSERT(dsm_safe_list_count(&list) == 3, "count == 3");

    int expected[] = {3, 2, 1};
    int idx = 0;
    item_t *pos;
    dsm_safe_list_for_each_entry(&list, pos, node) {
        TEST_ASSERT(pos->value == expected[idx], "ordre LIFO correct");
        idx++;
    }

    free_all(&list);
    dsm_safe_list_destroy(&list);
}

static void test_add_tail_order(void) {
    dsm_safe_list list;
    dsm_safe_list_init(&list);

    item_t *a = make_item(1);
    item_t *b = make_item(2);
    item_t *c = make_item(3);
    dsm_safe_list_add_tail(&list, &a->node);
    dsm_safe_list_add_tail(&list, &b->node);
    dsm_safe_list_add_tail(&list, &c->node);

    TEST_ASSERT(dsm_safe_list_count(&list) == 3, "count == 3 (add_tail)");

    int expected[] = {1, 2, 3};
    int idx = 0;
    item_t *pos;
    dsm_safe_list_for_each_entry(&list, pos, node) {
        TEST_ASSERT(pos->value == expected[idx], "ordre FIFO correct (add_tail)");
        idx++;
    }

    free_all(&list);
    dsm_safe_list_destroy(&list);
}

/* --- del ------------------------------------------------------------------ */

static void test_del_only_element(void) {
    dsm_safe_list list;
    dsm_safe_list_init(&list);

    item_t *it = make_item(99);
    dsm_safe_list_add(&list, &it->node);
    dsm_safe_list_del(&list, &it->node);

    TEST_ASSERT(dsm_safe_list_empty(&list), "liste vide après del du seul élément");
    TEST_ASSERT(dsm_safe_list_count(&list) == 0, "count == 0 après del");

    free(it);
    dsm_safe_list_destroy(&list);
}

static void test_del_middle_element(void) {
    dsm_safe_list list;
    dsm_safe_list_init(&list);

    item_t *a = make_item(1);
    item_t *b = make_item(2);
    item_t *c = make_item(3);
    dsm_safe_list_add_tail(&list, &a->node);
    dsm_safe_list_add_tail(&list, &b->node);
    dsm_safe_list_add_tail(&list, &c->node);

    dsm_safe_list_del(&list, &b->node); /* supprime l'élément du milieu */
    free(b);

    TEST_ASSERT(dsm_safe_list_count(&list) == 2, "count == 2 après del middle");

    int expected[] = {1, 3};
    int idx = 0;
    item_t *pos;
    dsm_safe_list_for_each_entry(&list, pos, node) {
        TEST_ASSERT(pos->value == expected[idx], "éléments restants corrects");
        idx++;
    }

    free_all(&list);
    dsm_safe_list_destroy(&list);
}

/* --- for_each_entry_safe avec suppression --------------------------------- */

static void test_safe_iteration_with_del(void) {
    dsm_safe_list list;
    dsm_safe_list_init(&list);

    for (int i = 0; i < 5; i++)
        dsm_safe_list_add_tail(&list, &make_item(i)->node);

    /* Supprimer les éléments pairs */
    item_t *pos, *n;
    dsm_safe_list_for_each_entry_safe(&list, pos, n, node) {
        if (pos->value % 2 == 0) {
            dsm_list_del(&pos->node); /* NOTE: dsm_list_del, pas dsm_safe_list_del */
            free(pos);
        }
    }

    TEST_ASSERT(dsm_safe_list_count(&list) == 2, "2 éléments impairs restants");

    int expected[] = {1, 3};
    int idx = 0;
    dsm_safe_list_for_each_entry(&list, pos, node) {
        TEST_ASSERT(pos->value == expected[idx], "valeur impaire correcte");
        idx++;
    }

    free_all(&list);
    dsm_safe_list_destroy(&list);
}

/* --- early-exit avec dsm_safe_list_unlock --------------------------------- */

static void test_early_exit_unlock(void) {
    dsm_safe_list list;
    dsm_safe_list_init(&list);

    for (int i = 0; i < 5; i++)
        dsm_safe_list_add_tail(&list, &make_item(i)->node);

    item_t *pos;
    int found = 0;
    dsm_safe_list_for_each_entry(&list, pos, node) {
        if (pos->value == 3) {
            found = 1;
            dsm_safe_list_unlock(&list); /* libérer le verrou avant break */
            break;
        }
    }

    TEST_ASSERT(found, "élément trouvé avec sortie anticipée");

    /* Le verrou doit être disponible pour une nouvelle opération */
    item_t *extra = make_item(99);
    dsm_safe_list_add(&list, &extra->node);
    TEST_ASSERT(dsm_safe_list_count(&list) == 6, "ajout post-break fonctionne");

    free_all(&list);
    dsm_safe_list_destroy(&list);
}

/* =========================================================================
 * Test multi-thread
 * ====================================================================== */

#define NTHREADS 8
#define ITEMS_PER_THREAD 200

typedef struct {
    dsm_safe_list *list;
    int base_value;
} thread_arg_t;

static void *producer(void *arg) {
    thread_arg_t *ta = arg;
    for (int i = 0; i < ITEMS_PER_THREAD; i++) {
        item_t *it = make_item(ta->base_value + i);
        dsm_safe_list_add_tail(ta->list, &it->node);
    }
    return NULL;
}

static void test_concurrent_add(void) {
    dsm_safe_list list;
    dsm_safe_list_init(&list);

    pthread_t threads[NTHREADS];
    thread_arg_t args[NTHREADS];

    for (int i = 0; i < NTHREADS; i++) {
        args[i].list = &list;
        args[i].base_value = i * ITEMS_PER_THREAD;
        pthread_create(&threads[i], NULL, producer, &args[i]);
    }
    for (int i = 0; i < NTHREADS; i++)
        pthread_join(threads[i], NULL);

    size_t expected = (size_t)(NTHREADS * ITEMS_PER_THREAD);
    TEST_ASSERT(dsm_safe_list_count(&list) == expected,
                "count correct après ajouts concurrents");

    free_all(&list);
    dsm_safe_list_destroy(&list);
}

static void *consumer_del(void *arg) {
    dsm_safe_list *list = arg;
    item_t *pos, *n;
    dsm_safe_list_for_each_entry_safe(list, pos, n, node) {
        dsm_list_del(&pos->node);
        free(pos);
    }
    return NULL;
}

static void test_concurrent_add_del(void) {
    dsm_safe_list list;
    dsm_safe_list_init(&list);

    /* Pré-remplissage */
    for (int i = 0; i < 500; i++)
        dsm_safe_list_add_tail(&list, &make_item(i)->node);

    /* Producteurs et consommateur simultanés */
    pthread_t prod[4], cons;
    thread_arg_t args[4];
    for (int i = 0; i < 4; i++) {
        args[i].list = &list;
        args[i].base_value = 1000 + i * 100;
        pthread_create(&prod[i], NULL, producer, &args[i]);
    }
    pthread_create(&cons, NULL, consumer_del, &list);

    for (int i = 0; i < 4; i++)
        pthread_join(prod[i], NULL);
    pthread_join(cons, NULL);

    /* Nettoyage du reste */
    free_all(&list);
    TEST_ASSERT(dsm_safe_list_empty(&list), "liste vide après add/del concurrents");
    dsm_safe_list_destroy(&list);
}

/* =========================================================================
 * Main
 * ====================================================================== */

int main(void) {
    printf("======================================\n");
    printf("  Tests dsm_safe_list\n");
    printf("======================================\n");

    RUN_TEST(test_init_destroy);
    RUN_TEST(test_destroy_null);
    RUN_TEST(test_add_one_element);
    RUN_TEST(test_add_fifo_order);
    RUN_TEST(test_add_tail_order);
    RUN_TEST(test_del_only_element);
    RUN_TEST(test_del_middle_element);
    RUN_TEST(test_safe_iteration_with_del);
    RUN_TEST(test_early_exit_unlock);
    RUN_TEST(test_concurrent_add);
    RUN_TEST(test_concurrent_add_del);

    printf("\n======================================\n");
    printf("  Résultats : %d/%d passés", g_tests_passed, g_tests_run);
    if (g_tests_failed)
        printf("  (%d ÉCHEC(S))", g_tests_failed);
    printf("\n======================================\n");

    return g_tests_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}