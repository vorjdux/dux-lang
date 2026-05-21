/*
 * test_lifetime.c — Stress tests for DuxStr reference-counting lifetime.
 *
 * Run under valgrind to verify zero leaks:
 *   valgrind --leak-check=full --error-exitcode=1 ./test_lifetime
 *
 * Each test section is designed so that valgrind will catch any refcount
 * over/under-flow: over-retained strings produce definite leaks, and
 * over-released strings (double-free) produce invalid-read errors.
 */

#include "duxrt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void check(int cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fflush(stderr);
        __builtin_trap();
    }
}

/* ── test_retain_release_balance ─────────────────────────────────────────── */
/*
 * Create a string, retain it N times, then release it N+1 times total
 * (one for the initial refcount of 1, N for each retain).
 * The string should be freed exactly once.
 */
static void test_retain_release_balance(void) {
#define N 20
    DuxStr* s = duxrt_str_new("balance_test", 12);
    check(s->refcount == 1, "balance: initial refcount != 1");

    for (int i = 0; i < N; i++)
        duxrt_str_retain(s);
    check(s->refcount == N + 1, "balance: refcount after retain wrong");

    /* Release N+1 times — last release frees the string */
    for (int i = 0; i <= N; i++)
        duxrt_str_release(s);
    /* s is now freed; do not dereference.  valgrind verifies exactly one free. */
#undef N
}

/* ── test_shared_callers ─────────────────────────────────────────────────── */
/*
 * Simulate passing a string to 3 "callers" (each retains) then each releases.
 * Verify that the string remains live throughout.
 */
static void test_shared_callers(void) {
    DuxStr* s = duxrt_str_new("shared string", 13);
    check(s->refcount == 1, "shared: initial refcount != 1");

    /* Three callers each retain */
    DuxStr* c1 = duxrt_str_retain(s);
    DuxStr* c2 = duxrt_str_retain(s);
    DuxStr* c3 = duxrt_str_retain(s);
    check(c1 == s && c2 == s && c3 == s, "shared: retain returned different pointer");
    check(s->refcount == 4, "shared: refcount after 3 retains wrong");

    /* Each caller verifies the string is still valid */
    check(strcmp(duxrt_str_cstr(c1), "shared string") == 0, "shared: c1 data wrong");
    check(strcmp(duxrt_str_cstr(c2), "shared string") == 0, "shared: c2 data wrong");
    check(strcmp(duxrt_str_cstr(c3), "shared string") == 0, "shared: c3 data wrong");

    /* Callers release one by one */
    duxrt_str_release(c1);
    check(s->refcount == 3, "shared: refcount after c1 release wrong");
    duxrt_str_release(c2);
    check(s->refcount == 2, "shared: refcount after c2 release wrong");
    duxrt_str_release(c3);
    check(s->refcount == 1, "shared: refcount after c3 release wrong");

    /* Original owner releases — frees */
    duxrt_str_release(s);
}

/* ── test_concat_chain_lifetime ──────────────────────────────────────────── */
/*
 * Concat chain: a + b → c
 * Release a and b; c should still be valid (it is a fresh allocation).
 */
static void test_concat_chain_lifetime(void) {
    DuxStr* a = duxrt_str_new("hello ", 6);
    DuxStr* b = duxrt_str_new("world",  5);
    DuxStr* c = duxrt_str_concat(a, b);
    check(c->refcount == 1, "concat_chain: c refcount != 1");
    check(c->len == 11,     "concat_chain: c len wrong");

    /* Release operands — c must remain valid because it's an independent alloc */
    duxrt_str_release(a);
    duxrt_str_release(b);

    /* c still accessible */
    check(strcmp(duxrt_str_cstr(c), "hello world") == 0, "concat_chain: c data wrong after releasing operands");

    /* Multi-level chain */
    DuxStr* d = duxrt_str_new("!", 1);
    DuxStr* e = duxrt_str_concat(c, d);
    duxrt_str_release(c);
    duxrt_str_release(d);

    check(e->len == 12, "concat_chain: e len wrong");
    check(strcmp(duxrt_str_cstr(e), "hello world!") == 0, "concat_chain: e data wrong");
    duxrt_str_release(e);
}

/* ── test_mass_create_release ────────────────────────────────────────────── */
/*
 * Create 10000 short strings and release each one immediately.
 * valgrind must report zero leaks.
 */
static void test_mass_create_release(void) {
#define MASS_N 10000
    for (int i = 0; i < MASS_N; i++) {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "str_%d", i);
        DuxStr* s = duxrt_str_new(buf, (int64_t)n);
        check(s != NULL, "mass: got NULL from str_new");
        check(s->refcount == 1, "mass: refcount != 1");
        duxrt_str_release(s);
    }
#undef MASS_N
}

/* ── test_mass_retain_pool ───────────────────────────────────────────────── */
/*
 * Create N strings into a pool, retain each once more, then release in
 * two passes (simulating two code paths that both hold references).
 * valgrind must report zero leaks.
 */
static void test_mass_retain_pool(void) {
#define POOL_N 500
    DuxStr* pool[POOL_N];

    for (int i = 0; i < POOL_N; i++) {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "pool_%d", i);
        pool[i] = duxrt_str_new(buf, (int64_t)n);
        duxrt_str_retain(pool[i]);   /* second owner */
        check(pool[i]->refcount == 2, "pool: refcount after retain wrong");
    }

    /* First pass: each "first owner" releases */
    for (int i = 0; i < POOL_N; i++) {
        duxrt_str_release(pool[i]);
        check(pool[i]->refcount == 1, "pool: first-pass refcount wrong");
    }

    /* Second pass: each "second owner" releases — frees the string */
    for (int i = 0; i < POOL_N; i++)
        duxrt_str_release(pool[i]);

#undef POOL_N
}

/* ── test_immortal_never_freed ───────────────────────────────────────────── */
/*
 * Retain and release an immortal string many times — it must never be freed.
 * The refcount stays at -1 throughout.
 */
static void test_immortal_never_freed(void) {
    static struct {
        int32_t refcount;
        int32_t len;
        char*   ext;
        char    data[8];
    } im_storage = { -1, 7, NULL, "forever" };
    DuxStr* im = (DuxStr*)&im_storage;

    for (int i = 0; i < 1000; i++) {
        duxrt_str_retain(im);
        duxrt_str_release(im);
    }
    check(im->refcount == -1, "immortal: refcount changed from -1");
    check(strcmp(duxrt_str_cstr(im), "forever") == 0, "immortal: data corrupted");
}

/* ── test_long_string_lifetime ───────────────────────────────────────────── */
/*
 * Long strings (> 63 bytes) have a separate ext buffer.
 * Verify the ext buffer is freed exactly once on the final release.
 * (valgrind confirms no double-free and no leak)
 */
static void test_long_string_lifetime(void) {
    const char* src =
        "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
        "YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY";
    int64_t len = (int64_t)strlen(src);
    DuxStr* s = duxrt_str_new(src, len);
    check(s->ext != NULL, "long_lifetime: ext should be non-NULL");

    duxrt_str_retain(s);
    duxrt_str_retain(s);
    check(s->refcount == 3, "long_lifetime: refcount after 2 retains wrong");

    duxrt_str_release(s);
    duxrt_str_release(s);
    check(s->refcount == 1, "long_lifetime: refcount before final release wrong");

    duxrt_str_release(s);
    /* ext buffer + header freed exactly once; valgrind verifies */
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void) {
    test_retain_release_balance();
    test_shared_callers();
    test_concat_chain_lifetime();
    test_mass_create_release();
    test_mass_retain_pool();
    test_immortal_never_freed();
    test_long_string_lifetime();
    printf("test_lifetime: all tests passed\n");
    return 0;
}
