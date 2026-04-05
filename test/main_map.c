// #include "debug/debug.h"

#include <stdio.h>
#include <assert.h>
#include "util/hashmap.h"
struct item {
    int                   id;
    struct dsm_hlist_node node;
};

DSM_DEFINE_HASHTABLE(table, 4); /* 16 buckets */

int main(int argc, char const *argv[]) {

    struct item a = { .id = 1 };
    struct item b = { .id = 2 };
    struct item c = { .id = 1 }; /* collision volontaire avec a */

    /* ── Insertion ── */
    dsm_hash_add(table, &a.node, a.id);
    dsm_hash_add(table, &b.node, b.id);
    dsm_hash_add(table, &c.node, c.id);
    printf("[OK] insert\n");

    /* ── Recherche : id=1, doit trouver 2 entrées ── */
    struct item *p;
    int count = 0;
    dsm_hash_for_each_possible(table, p, node, 1) {
        assert(p->id == 1);
        count++;
    }
    assert(count == 2);
    printf("[OK] lookup  (found %d entries for key=1)\n", count);

    /* ── Non-vide ── */
    assert(!dsm_hash_empty(table));
    printf("[OK] not empty\n");

    /* ── Suppression ── */
    dsm_hash_del(&a.node);
    dsm_hash_del(&b.node);
    dsm_hash_del(&c.node);

    /* ── Vide ── */
    assert(dsm_hash_empty(table));
    printf("[OK] empty after delete\n");

    printf("\nAll tests passed.\n");
    return 0;
}
