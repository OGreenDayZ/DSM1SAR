/**
 * @file test_safe_map.c
 * @brief Tests unitaires pour dsm_safe_map
 *
 * Compilation :
 *   gcc -Wall -Wextra -pthread -o test_safe_map test_safe_map.c -I.
 * Exécution :
 *   ./test_safe_map
 */

#include "util/concurrent/safe_map.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    uint32_t key;
    int value;
    struct dsm_hlist_node node;
} item_t;

static item_t *make_item(uint32_t key, int value) {
    item_t *it = malloc(sizeof(item_t));
    if (!it) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    it->key = key;
    it->value = value;
    dsm_init_hlist_node(&it->node);
    return it;
}

/** Vide et libère tous les éléments de la map */
static void free_all(dsm_safe_map *m) {
    unsigned int bkt;
    struct dsm_hlist_node *tmp;
    item_t *obj;
    dsm_hash_safe_for_each_safe(m, bkt, tmp, obj, node) {
        dsm_hlist_del_init(&obj->node);
        free(obj);
    }
}

/* =========================================================================
 * Tests unitaires séquentiels
 * ====================================================================== */

/* --- init / destroy ------------------------------------------------------- */

static void test_init_destroy(void) {
    dsm_safe_map m;
    int ret = dsm_safe_map_init(&m, 4); /* 16 buckets */
    TEST_ASSERT(ret == 0, "dsm_safe_map_init retourne 0");
    TEST_ASSERT(dsm_safe_map_empty(&m), "map vide après init");
    TEST_ASSERT(dsm_safe_map_count(&m) == 0, "count == 0 après init");
    dsm_safe_map_destroy(&m);
    TEST_ASSERT(1, "dsm_safe_map_destroy ne crashe pas");
}

static void test_destroy_null(void) {
    dsm_safe_map_destroy(NULL);
    TEST_ASSERT(1, "dsm_safe_map_destroy(NULL) ne crashe pas");
}

static void test_init_various_bits(void) {
    dsm_safe_map m;
    for (unsigned int bits = 1; bits <= 8; bits++) {
        int ret = dsm_safe_map_init(&m, bits);
        TEST_ASSERT(ret == 0, "init OK pour différentes tailles de bits");
        dsm_safe_map_destroy(&m);
    }
}

/* --- add / del / empty / count ------------------------------------------- */

static void test_add_one(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 4);

    item_t *it = make_item(42, 100);
    dsm_hash_safe_add(&m, &it->node, it->key);

    TEST_ASSERT(!dsm_safe_map_empty(&m), "map non-vide après add");
    TEST_ASSERT(dsm_safe_map_count(&m) == 1, "count == 1 après add");

    free_all(&m);
    dsm_safe_map_destroy(&m);
}

static void test_add_multiple(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 4);

    int n = 20;
    for (int i = 0; i < n; i++) {
        item_t *it = make_item((uint32_t)i, i * 10);
        dsm_hash_safe_add(&m, &it->node, it->key);
    }

    TEST_ASSERT(dsm_safe_map_count(&m) == (size_t)n,
                "count correct après ajouts multiples");
    TEST_ASSERT(!dsm_safe_map_empty(&m), "map non-vide après ajouts multiples");

    free_all(&m);
    dsm_safe_map_destroy(&m);
}

static void test_del_single(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 4);

    item_t *it = make_item(7, 77);
    dsm_hash_safe_add(&m, &it->node, it->key);
    dsm_hash_safe_del(&m, &it->node, it->key);

    TEST_ASSERT(dsm_safe_map_empty(&m), "map vide après del du seul élément");
    TEST_ASSERT(dsm_safe_map_count(&m) == 0, "count == 0 après del");
    TEST_ASSERT(dsm_hlist_unhashed(&it->node), "nœud unhashed après del");

    free(it);
    dsm_safe_map_destroy(&m);
}

static void test_del_one_of_many(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 4);

    item_t *items[5];
    for (int i = 0; i < 5; i++) {
        items[i] = make_item((uint32_t)i, i);
        dsm_hash_safe_add(&m, &items[i]->node, items[i]->key);
    }

    /* Supprimer l'élément d'index 2 */
    dsm_hash_safe_del(&m, &items[2]->node, items[2]->key);
    free(items[2]);

    TEST_ASSERT(dsm_safe_map_count(&m) == 4, "count == 4 après del d'un élément");

    free_all(&m);
    dsm_safe_map_destroy(&m);
}

static void test_del_idempotent(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 4);

    item_t *it = make_item(99, 0);
    dsm_hash_safe_add(&m, &it->node, it->key);
    dsm_hash_safe_del(&m, &it->node, it->key);
    /* dsm_hlist_del_init est idempotent : double suppression ne doit pas crasher */
    dsm_hash_safe_del(&m, &it->node, it->key);

    TEST_ASSERT(dsm_safe_map_empty(&m), "map vide après double del");

    free(it);
    dsm_safe_map_destroy(&m);
}

/* --- collisions (même bucket) -------------------------------------------- */

static void test_same_bucket_collision(void) {
    /* Avec bits=1 (2 buckets), des clés différentes peuvent collisionner */
    dsm_safe_map m;
    dsm_safe_map_init(&m, 1);

    item_t *a = make_item(0, 10);
    item_t *b = make_item(2, 20); /* hash_min(0,1)==hash_min(2,1) possible */
    item_t *c = make_item(4, 30);
    dsm_hash_safe_add(&m, &a->node, a->key);
    dsm_hash_safe_add(&m, &b->node, b->key);
    dsm_hash_safe_add(&m, &c->node, c->key);

    TEST_ASSERT(dsm_safe_map_count(&m) == 3,
                "3 éléments présents malgré collisions de bucket");

    free_all(&m);
    dsm_safe_map_destroy(&m);
}

/* --- for_each / for_each_safe -------------------------------------------- */

static void test_for_each_visit_all(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 4);

    int n = 10;
    for (int i = 0; i < n; i++) {
        item_t *it = make_item((uint32_t)i, i);
        dsm_hash_safe_add(&m, &it->node, it->key);
    }

    int count = 0;
    unsigned int bkt;
    item_t *obj;
    dsm_hash_safe_for_each(&m, bkt, obj, node) count++;

    TEST_ASSERT(count == n, "for_each visite tous les éléments");

    free_all(&m);
    dsm_safe_map_destroy(&m);
}

static void test_for_each_safe_del(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 4);

    for (int i = 0; i < 10; i++) {
        item_t *it = make_item((uint32_t)i, i);
        dsm_hash_safe_add(&m, &it->node, it->key);
    }

    /* Supprimer les éléments de valeur paire pendant l'itération */
    unsigned int bkt;
    struct dsm_hlist_node *tmp;
    item_t *obj;
    dsm_hash_safe_for_each_safe(&m, bkt, tmp, obj, node) {
        if (obj->value % 2 == 0) {
            dsm_hlist_del_init(&obj->node); /* PAS dsm_hash_safe_del ! */
            free(obj);
        }
    }

    TEST_ASSERT(dsm_safe_map_count(&m) == 5,
                "5 éléments impairs restants après suppression dans for_each_safe");

    free_all(&m);
    dsm_safe_map_destroy(&m);
}

/* --- dsm_hash_safe_get --------------------------------------------------- */

static void test_get_found(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 4);

    item_t *it = make_item(123, 456);
    dsm_hash_safe_add(&m, &it->node, it->key);

    item_t *obj;
    int found = 0;
    dsm_hash_safe_get(&m, obj, node, 123) {
        if (obj->key == 123 && obj->value == 456)
            found = 1;
    }
    dsm_hash_safe_get_end(&m, 123);

    TEST_ASSERT(found, "get trouve l'élément existant");

    free_all(&m);
    dsm_safe_map_destroy(&m);
}

static void test_get_not_found(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 4);

    item_t *obj;
    int found = 0;
    dsm_hash_safe_get(&m, obj, node, 999) {
        found = 1; /* ne doit jamais s'exécuter */
    }
    dsm_hash_safe_get_end(&m, 999);

    TEST_ASSERT(!found, "get ne trouve rien dans une map vide");

    dsm_safe_map_destroy(&m);
}

static void test_get_multiple_same_bucket(void) {
    /* Insérer plusieurs éléments et vérifier que get les retrouve tous */
    dsm_safe_map m;
    dsm_safe_map_init(&m, 1); /* 2 buckets → collisions fréquentes */

    uint32_t keys[] = {0, 1, 2, 3, 4};
    int n = 5;
    for (int i = 0; i < n; i++) {
        item_t *it = make_item(keys[i], (int)keys[i] * 10);
        dsm_hash_safe_add(&m, &it->node, it->key);
    }

    /* Chercher chaque clé individuellement */
    int found_count = 0;
    for (int i = 0; i < n; i++) {
        item_t *obj;
        dsm_hash_safe_get(&m, obj, node, keys[i]) {
            if (obj->key == keys[i])
                found_count++;
        }
        dsm_hash_safe_get_end(&m, keys[i]);
    }

    TEST_ASSERT(found_count == n, "get retrouve chaque clé insérée");

    free_all(&m);
    dsm_safe_map_destroy(&m);
}

/* --- dsm_hash_safe_unlock (early exit) ------------------------------------ */

static void test_for_each_early_exit(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 4);

    for (int i = 0; i < 10; i++) {
        item_t *it = make_item((uint32_t)i, i);
        dsm_hash_safe_add(&m, &it->node, it->key);
    }

    unsigned int bkt;
    item_t *obj;
    int found = 0;
    dsm_hash_safe_for_each(&m, bkt, obj, node) {
        if (obj->value == 5) {
            found = 1;
            dsm_hash_safe_unlock(&m, bkt); /* libérer le verrou avant break */
            break;
        }
    }

    TEST_ASSERT(found, "early exit : élément trouvé");

    /* Le verrou doit être disponible : tenter un ajout */
    item_t *extra = make_item(100, 999);
    dsm_hash_safe_add(&m, &extra->node, extra->key);
    TEST_ASSERT(dsm_safe_map_count(&m) == 11,
                "ajout post-break fonctionne (verrou bien relâché)");

    free_all(&m);
    dsm_safe_map_destroy(&m);
}

/* =========================================================================
 * Tests multi-thread
 * ====================================================================== */

#define NTHREADS 8
#define ITEMS_PER_THREAD 200

typedef struct {
    dsm_safe_map *map;
    uint32_t base_key;
    int base_value;
} thread_arg_t;

static void *producer(void *arg) {
    thread_arg_t *ta = arg;
    for (int i = 0; i < ITEMS_PER_THREAD; i++) {
        item_t *it = make_item(ta->base_key + (uint32_t)i, ta->base_value + i);
        dsm_hash_safe_add(ta->map, &it->node, it->key);
    }
    return NULL;
}

static void test_concurrent_add(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 6); /* 64 buckets */

    pthread_t threads[NTHREADS];
    thread_arg_t args[NTHREADS];

    for (int i = 0; i < NTHREADS; i++) {
        args[i].map = &m;
        args[i].base_key = (uint32_t)(i * ITEMS_PER_THREAD);
        args[i].base_value = i * ITEMS_PER_THREAD;
        pthread_create(&threads[i], NULL, producer, &args[i]);
    }
    for (int i = 0; i < NTHREADS; i++)
        pthread_join(threads[i], NULL);

    size_t expected = (size_t)(NTHREADS * ITEMS_PER_THREAD);
    TEST_ASSERT(dsm_safe_map_count(&m) == expected,
                "count correct après ajouts concurrents");

    free_all(&m);
    dsm_safe_map_destroy(&m);
}

static void *consumer_del(void *arg) {
    dsm_safe_map *m = arg;
    unsigned int bkt;
    struct dsm_hlist_node *tmp;
    item_t *obj;
    dsm_hash_safe_for_each_safe(m, bkt, tmp, obj, node) {
        dsm_hlist_del_init(&obj->node);
        free(obj);
    }
    return NULL;
}

static void test_concurrent_add_del(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 6);

    /* Pré-remplissage */
    for (int i = 0; i < 500; i++) {
        item_t *it = make_item((uint32_t)i, i);
        dsm_hash_safe_add(&m, &it->node, it->key);
    }

    pthread_t prod[4], cons;
    thread_arg_t args[4];
    for (int i = 0; i < 4; i++) {
        args[i].map = &m;
        args[i].base_key = (uint32_t)(1000 + i * 100);
        args[i].base_value = 1000 + i * 100;
        pthread_create(&prod[i], NULL, producer, &args[i]);
    }
    pthread_create(&cons, NULL, consumer_del, &m);

    for (int i = 0; i < 4; i++)
        pthread_join(prod[i], NULL);
    pthread_join(cons, NULL);

    /* Nettoyage du reste */
    free_all(&m);
    TEST_ASSERT(dsm_safe_map_empty(&m), "map vide après add/del concurrents");
    dsm_safe_map_destroy(&m);
}

static void *reader(void *arg) {
    dsm_safe_map *m = arg;
    unsigned int bkt;
    item_t *obj;
    size_t count = 0;
    dsm_hash_safe_for_each(m, bkt, obj, node) count++;
    return (void *)(uintptr_t)count;
}

static void test_concurrent_readers(void) {
    dsm_safe_map m;
    dsm_safe_map_init(&m, 4);

    int n = 50;
    for (int i = 0; i < n; i++) {
        item_t *it = make_item((uint32_t)i, i);
        dsm_hash_safe_add(&m, &it->node, it->key);
    }

    pthread_t readers[NTHREADS];
    for (int i = 0; i < NTHREADS; i++)
        pthread_create(&readers[i], NULL, reader, &m);

    int all_ok = 1;
    for (int i = 0; i < NTHREADS; i++) {
        void *ret;
        pthread_join(readers[i], &ret);
        if ((size_t)(uintptr_t)ret != (size_t)n)
            all_ok = 0;
    }
    TEST_ASSERT(all_ok, "tous les lecteurs concurrents voient le bon count");

    free_all(&m);
    dsm_safe_map_destroy(&m);
}

/* =========================================================================
 * Main
 * ====================================================================== */

int main(void) {
    printf("======================================\n");
    printf("  Tests dsm_safe_map\n");
    printf("======================================\n");

    RUN_TEST(test_init_destroy);
    RUN_TEST(test_destroy_null);
    RUN_TEST(test_init_various_bits);

    RUN_TEST(test_add_one);
    RUN_TEST(test_add_multiple);
    RUN_TEST(test_del_single);
    RUN_TEST(test_del_one_of_many);
    RUN_TEST(test_del_idempotent);

    RUN_TEST(test_same_bucket_collision);

    RUN_TEST(test_for_each_visit_all);
    RUN_TEST(test_for_each_safe_del);

    RUN_TEST(test_get_found);
    RUN_TEST(test_get_not_found);
    RUN_TEST(test_get_multiple_same_bucket);

    RUN_TEST(test_for_each_early_exit);

    RUN_TEST(test_concurrent_add);
    RUN_TEST(test_concurrent_add_del);
    RUN_TEST(test_concurrent_readers);

    printf("\n======================================\n");
    printf("  Résultats : %d/%d passés", g_tests_passed, g_tests_run);
    if (g_tests_failed)
        printf("  (%d ÉCHEC(S))", g_tests_failed);
    printf("\n======================================\n");

    return g_tests_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}