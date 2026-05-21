/*
 * test_dict.c — Tests for DuxDict open-addressing hash map.
 *
 * Run under valgrind to check for leaks:
 *   valgrind --leak-check=full --error-exitcode=1 ./test_dict
 *
 * Notes:
 *  - The tombstone correctness test verifies that deleting a key in the middle
 *    of a probe chain does not break lookup of keys displaced past it.
 *  - Key leak on update is best verified with valgrind: every strdup'd key on
 *    the update path must be freed inside dict_insert_raw.
 */

#include "duxrt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void check(int cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fflush(stderr);
        __builtin_trap();
    }
}

/* ── test_insert_get ─────────────────────────────────────────────────────── */
static void test_insert_get(void) {
    DuxDict* d = duxrt_dict_new();
    check(d != NULL, "insert_get: dict_new returned NULL");

    int v1 = 10, v2 = 20, v3 = 30;
    duxrt_dict_set(d, "alpha",   &v1);
    duxrt_dict_set(d, "beta",    &v2);
    duxrt_dict_set(d, "gamma",   &v3);

    check(duxrt_dict_get(d, "alpha")  == &v1, "insert_get: alpha wrong");
    check(duxrt_dict_get(d, "beta")   == &v2, "insert_get: beta wrong");
    check(duxrt_dict_get(d, "gamma")  == &v3, "insert_get: gamma wrong");
    check(duxrt_dict_get(d, "delta")  == NULL, "insert_get: missing key should return NULL");

    check(duxrt_dict_has(d, "alpha")  == 1, "insert_get: has alpha");
    check(duxrt_dict_has(d, "delta")  == 0, "insert_get: has missing key");

    duxrt_dict_free(d);
}

/* ── test_update ─────────────────────────────────────────────────────────── */
/*
 * Update an existing key and verify the value changes.
 * Run under valgrind to confirm no key leak: the strdup'd key on the update
 * path must be freed inside dict_insert_raw, not stored as a second copy.
 */
static void test_update(void) {
    DuxDict* d = duxrt_dict_new();

    int old_val = 1, new_val = 2;
    duxrt_dict_set(d, "key", &old_val);
    check(duxrt_dict_get(d, "key") == &old_val, "update: initial value wrong");

    duxrt_dict_set(d, "key", &new_val);
    check(duxrt_dict_get(d, "key") == &new_val, "update: updated value wrong");

    /* Update many times — valgrind checks no leak accumulates */
    for (int i = 0; i < 100; i++) {
        duxrt_dict_set(d, "key", &old_val);
        duxrt_dict_set(d, "key", &new_val);
    }
    check(duxrt_dict_get(d, "key") == &new_val, "update: final value wrong");

    duxrt_dict_free(d);
}

/* ── test_delete ─────────────────────────────────────────────────────────── */
static void test_delete(void) {
    DuxDict* d = duxrt_dict_new();

    int v = 42;
    duxrt_dict_set(d, "erase_me", &v);
    check(duxrt_dict_get(d, "erase_me") == &v, "delete: pre-delete lookup failed");

    duxrt_dict_del(d, "erase_me");
    check(duxrt_dict_get(d, "erase_me")  == NULL, "delete: key should be gone after delete");
    check(duxrt_dict_has(d, "erase_me")  == 0,    "delete: has should return 0 after delete");

    /* Deleting a non-existent key should be a no-op */
    duxrt_dict_del(d, "never_existed");

    duxrt_dict_free(d);
}

/* ── test_tombstone_probe_chain ──────────────────────────────────────────── */
/*
 * This test exercises the tombstone correctness fix.  We need two keys that
 * hash to the same slot so that the second is displaced into the slot right
 * after the first.  Then we delete the first key (creating a tombstone) and
 * verify the second key is still findable.
 *
 * Instead of relying on hash collisions (which depend on the FNV hash internals
 * and table size), we fill the table densely so that many linear-probe chains
 * form naturally, then delete every other key and re-look up all remaining keys.
 */
static void test_tombstone_probe_chain(void) {
    DuxDict* d = duxrt_dict_new();

#define NKEYS 50
    /* Use a fixed set of string keys */
    static const char* keys[NKEYS] = {
        "k00","k01","k02","k03","k04","k05","k06","k07","k08","k09",
        "k10","k11","k12","k13","k14","k15","k16","k17","k18","k19",
        "k20","k21","k22","k23","k24","k25","k26","k27","k28","k29",
        "k30","k31","k32","k33","k34","k35","k36","k37","k38","k39",
        "k40","k41","k42","k43","k44","k45","k46","k47","k48","k49",
    };
    int vals[NKEYS];
    for (int i = 0; i < NKEYS; i++) {
        vals[i] = i * 7;
        duxrt_dict_set(d, keys[i], &vals[i]);
    }

    /* Verify all present */
    for (int i = 0; i < NKEYS; i++)
        check(duxrt_dict_get(d, keys[i]) == &vals[i], "tombstone: pre-delete get failed");

    /* Delete even-indexed keys (creating tombstones throughout the table) */
    for (int i = 0; i < NKEYS; i += 2)
        duxrt_dict_del(d, keys[i]);

    /* Odd-indexed keys must still be findable through the tombstones */
    for (int i = 1; i < NKEYS; i += 2)
        check(duxrt_dict_get(d, keys[i]) == &vals[i], "tombstone: post-delete odd key lost");

    /* Even-indexed keys must be truly gone */
    for (int i = 0; i < NKEYS; i += 2)
        check(duxrt_dict_get(d, keys[i]) == NULL, "tombstone: deleted key still present");

#undef NKEYS
    duxrt_dict_free(d);
}

/* ── test_resize ─────────────────────────────────────────────────────────── */
/*
 * Insert enough keys to trigger at least one resize (load factor 3/4).
 * The initial capacity is 16, so we need > 12 entries to trigger the first
 * resize.  Insert 200 entries to force multiple resizes.
 */
static void test_resize(void) {
    DuxDict* d = duxrt_dict_new();

#define RESIZE_N 200
    int vals[RESIZE_N];
    char keys[RESIZE_N][16];
    for (int i = 0; i < RESIZE_N; i++) {
        snprintf(keys[i], sizeof(keys[i]), "key%d", i);
        vals[i] = i * 3;
        duxrt_dict_set(d, keys[i], &vals[i]);
    }

    /* Every key must be retrievable after resize */
    for (int i = 0; i < RESIZE_N; i++)
        check(duxrt_dict_get(d, keys[i]) == &vals[i], "resize: key lost after resize");

#undef RESIZE_N
    duxrt_dict_free(d);
}

/* ── test_free_releases_memory ───────────────────────────────────────────── */
/*
 * Create a dict, insert entries, then free it.
 * Valgrind verifies all internal key copies are freed.
 */
static void test_free_releases_memory(void) {
    DuxDict* d = duxrt_dict_new();
    int dummy = 0;
    for (int i = 0; i < 32; i++) {
        char k[16];
        snprintf(k, sizeof(k), "item%d", i);
        duxrt_dict_set(d, k, &dummy);
    }
    /* Delete half the entries (creates tombstones) — free must skip them */
    for (int i = 0; i < 32; i += 2) {
        char k[16];
        snprintf(k, sizeof(k), "item%d", i);
        duxrt_dict_del(d, k);
    }
    duxrt_dict_free(d);
    /* valgrind will catch any leaked key copies or the entry array */
}

/* ── test_null_safety ────────────────────────────────────────────────────── */
static void test_null_safety(void) {
    /* NULL dict must be handled gracefully */
    duxrt_dict_set(NULL, "k", NULL);
    check(duxrt_dict_get(NULL, "k") == NULL, "null safety: get(NULL,k)");
    check(duxrt_dict_has(NULL, "k") == 0,    "null safety: has(NULL,k)");
    duxrt_dict_del(NULL, "k");
    duxrt_dict_free(NULL);

    /* NULL key on a valid dict must also be handled gracefully */
    DuxDict* d = duxrt_dict_new();
    duxrt_dict_set(d, NULL, NULL);
    check(duxrt_dict_get(d, NULL) == NULL, "null safety: get(d,NULL)");
    check(duxrt_dict_has(d, NULL) == 0,    "null safety: has(d,NULL)");
    duxrt_dict_del(d, NULL);
    duxrt_dict_free(d);
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void) {
    test_insert_get();
    test_update();
    test_delete();
    test_tombstone_probe_chain();
    test_resize();
    test_free_releases_memory();
    test_null_safety();
    printf("test_dict: all tests passed\n");
    return 0;
}
