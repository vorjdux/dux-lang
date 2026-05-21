/*
 * test_list.c — Tests for DuxList dynamic array.
 *
 * Run under valgrind to check for leaks:
 *   valgrind --leak-check=full --error-exitcode=1 ./test_list
 *
 * Notes:
 *  - Out-of-bounds access calls abort(), so it cannot be tested in-process
 *    without a fork/signal harness.  Those cases are documented but skipped
 *    in the normal test run.
 */

#include "duxrt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fflush(stderr);
        __builtin_trap();
    }
}

/* ── test_push_get_len ───────────────────────────────────────────────────── */
static void test_push_get_len(void) {
    DuxList* l = duxrt_list_new();
    check(l != NULL,              "push_get_len: list_new returned NULL");
    check(duxrt_list_len(l) == 0, "push_get_len: initial len != 0");

    int a = 1, b = 2, c = 3;
    duxrt_list_push(l, &a);
    duxrt_list_push(l, &b);
    duxrt_list_push(l, &c);

    check(duxrt_list_len(l) == 3,   "push_get_len: len should be 3");
    check(duxrt_list_get(l, 0) == &a, "push_get_len: [0] wrong");
    check(duxrt_list_get(l, 1) == &b, "push_get_len: [1] wrong");
    check(duxrt_list_get(l, 2) == &c, "push_get_len: [2] wrong");

    duxrt_list_free(l);
}

/* ── test_set ────────────────────────────────────────────────────────────── */
static void test_set(void) {
    DuxList* l = duxrt_list_new();

    int old = 10, newer = 99;
    duxrt_list_push(l, &old);
    check(duxrt_list_get(l, 0) == &old, "set: initial get wrong");

    duxrt_list_set(l, 0, &newer);
    check(duxrt_list_get(l, 0) == &newer, "set: value not updated");

    duxrt_list_free(l);
}

/* ── test_negative_index ─────────────────────────────────────────────────── */
static void test_negative_index(void) {
    DuxList* l = duxrt_list_new();

    int a = 10, b = 20, c = 30;
    duxrt_list_push(l, &a);
    duxrt_list_push(l, &b);
    duxrt_list_push(l, &c);

    /* Negative indexing: -1 is last, -3 is first */
    check(duxrt_list_get(l, -1) == &c, "neg_index: [-1] should be last");
    check(duxrt_list_get(l, -2) == &b, "neg_index: [-2] should be middle");
    check(duxrt_list_get(l, -3) == &a, "neg_index: [-3] should be first");

    /* Set via negative index */
    int replacement = 999;
    duxrt_list_set(l, -1, &replacement);
    check(duxrt_list_get(l, 2) == &replacement, "neg_index: set[-1] not reflected at [2]");

    duxrt_list_free(l);
}

/* ── test_grow ───────────────────────────────────────────────────────────── */
/*
 * Push enough elements to force multiple doublings of the internal buffer
 * (initial capacity is 8).  All elements must remain accessible afterward.
 */
static void test_grow(void) {
    DuxList* l = duxrt_list_new();

#define NGROW 256
    int vals[NGROW];
    for (int i = 0; i < NGROW; i++) {
        vals[i] = i * 5;
        duxrt_list_push(l, &vals[i]);
    }

    check(duxrt_list_len(l) == NGROW, "grow: len wrong after many pushes");
    for (int i = 0; i < NGROW; i++)
        check(duxrt_list_get(l, (int64_t)i) == &vals[i], "grow: element wrong after resize");

#undef NGROW
    duxrt_list_free(l);
}

/* ── test_free_releases_memory ───────────────────────────────────────────── */
/*
 * Verified by valgrind — no in-process check for actual deallocation.
 */
static void test_free_releases_memory(void) {
    DuxList* l = duxrt_list_new();
    int dummy = 0;
    for (int i = 0; i < 64; i++)
        duxrt_list_push(l, &dummy);
    duxrt_list_free(l);
    /* valgrind catches any leak in the data array or list header */
}

/* ── test_len_null_safe ──────────────────────────────────────────────────── */
static void test_len_null_safe(void) {
    check(duxrt_list_len(NULL) == 0, "len(NULL) should be 0");
}

/*
 * Out-of-bounds tests are omitted from normal runs because they call abort().
 * To test them, use a fork+waitpid harness and assert the child exits with
 * a signal.  Example (not run here):
 *
 *   pid_t pid = fork();
 *   if (pid == 0) { duxrt_list_get(l, 9999); _exit(0); } // should abort
 *   int st; waitpid(pid, &st, 0);
 *   assert(WIFSIGNALED(st)); // SIGABRT
 */

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void) {
    test_push_get_len();
    test_set();
    test_negative_index();
    test_grow();
    test_free_releases_memory();
    test_len_null_safe();
    printf("test_list: all tests passed\n");
    return 0;
}
