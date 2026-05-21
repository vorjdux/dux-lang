/*
 * test_str.c — Tests for DuxStr reference-counted strings.
 *
 * Run under valgrind to check for leaks:
 *   valgrind --leak-check=full --error-exitcode=1 ./test_str
 *
 * Notes:
 *  - Overflow guard (len > INT32_MAX should abort) is not exercised in-process
 *    because it calls abort(). It can be tested via a fork+signal harness or
 *    by a separate driver that expects a non-zero exit code.
 *  - Immortal strings (refcount == -1) have retain/release treated as no-ops;
 *    they are typically allocated as static globals in generated code and never
 *    freed by the runtime.
 */

#include "duxrt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ── helpers ─────────────────────────────────────────────────────────────── */

/* Build an immortal (static) DuxStr — matches what codegen emits for literals */
#define MAKE_IMMORTAL(var, literal)                                     \
    static struct {                                                      \
        int32_t refcount;                                               \
        int32_t len;                                                    \
        char*   ext;                                                    \
        char    data[sizeof(literal)];                                  \
    } var##_storage = { -1, (int32_t)(sizeof(literal)-1), NULL, literal }; \
    DuxStr* var = (DuxStr*)&var##_storage

static void check(int cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        /* flush then abort so valgrind reports leaks from this test run */
        fflush(stderr);
        __builtin_trap();
    }
}

/* ── test_new_short ──────────────────────────────────────────────────────── */
static void test_new_short(void) {
    /* String of length <= 63 should be stored inline (ext == NULL) */
    DuxStr* s = duxrt_str_new("hello", 5);
    check(s != NULL,           "new_short: got NULL");
    check(s->refcount == 1,    "new_short: refcount != 1");
    check(s->len == 5,         "new_short: len != 5");
    check(s->ext == NULL,      "new_short: ext should be NULL for short string");
    check(strcmp(s->data, "hello") == 0, "new_short: data mismatch");
    check(duxrt_str_length(s) == 5, "new_short: duxrt_str_length wrong");
    duxrt_str_release(s);
}

/* ── test_new_long ───────────────────────────────────────────────────────── */
static void test_new_long(void) {
    /* String of length > 64 should use ext buffer */
    const char* src =
        "This is a string that is definitely longer than sixty-three characters, yes.";
    int64_t slen = (int64_t)strlen(src);
    check(slen > 64, "test setup: src must be > 64 chars");
    DuxStr* s = duxrt_str_new(src, slen);
    check(s != NULL,           "new_long: got NULL");
    check(s->refcount == 1,    "new_long: refcount != 1");
    check(s->len == (int32_t)slen, "new_long: len mismatch");
    check(s->ext != NULL,      "new_long: ext should be non-NULL for long string");
    check(strcmp(s->ext, src) == 0, "new_long: ext data mismatch");
    check(duxrt_str_length(s) == slen, "new_long: duxrt_str_length wrong");
    duxrt_str_release(s);
}

/* ── test_retain_refcount ────────────────────────────────────────────────── */
static void test_retain_refcount(void) {
    DuxStr* s = duxrt_str_new("abc", 3);
    check(s->refcount == 1, "retain: initial refcount != 1");
    DuxStr* r = duxrt_str_retain(s);
    check(r == s,           "retain: returned different pointer");
    check(s->refcount == 2, "retain: refcount not incremented");
    duxrt_str_release(s);   /* drop to 1 */
    check(s->refcount == 1, "retain: after one release refcount != 1");
    duxrt_str_release(s);   /* drop to 0 — frees */
}

/* ── test_release_frees ──────────────────────────────────────────────────── */
/*
 * We cannot verify in-process that the memory was freed (would require hooking
 * malloc).  Run this test under valgrind: if release does not free, valgrind
 * will report a definite leak.
 */
static void test_release_frees(void) {
    /* Short string */
    DuxStr* s = duxrt_str_new("leak_me_not", 11);
    duxrt_str_release(s);

    /* Long string */
    const char* long_src =
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    DuxStr* l = duxrt_str_new(long_src, (int64_t)strlen(long_src));
    duxrt_str_release(l);
}

/* ── test_concat ─────────────────────────────────────────────────────────── */
static void test_concat_short_short(void) {
    DuxStr* a = duxrt_str_new("foo", 3);
    DuxStr* b = duxrt_str_new("bar", 3);
    DuxStr* c = duxrt_str_concat(a, b);
    check(c->len == 6, "concat ss: len != 6");
    check(strcmp(duxrt_str_cstr(c), "foobar") == 0, "concat ss: data wrong");
    duxrt_str_release(a);
    duxrt_str_release(b);
    duxrt_str_release(c);
}

static void test_concat_short_long(void) {
    DuxStr* a = duxrt_str_new("prefix:", 7);
    const char* long_src =
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
    int64_t llen = (int64_t)strlen(long_src);
    DuxStr* b = duxrt_str_new(long_src, llen);
    DuxStr* c = duxrt_str_concat(a, b);
    check(c->len == 7 + (int32_t)llen, "concat sl: len wrong");
    const char* cs = duxrt_str_cstr(c);
    check(strncmp(cs, "prefix:", 7) == 0, "concat sl: prefix wrong");
    check(strcmp(cs + 7, long_src) == 0, "concat sl: suffix wrong");
    duxrt_str_release(a);
    duxrt_str_release(b);
    duxrt_str_release(c);
}

static void test_concat_long_long(void) {
    const char* s1 =
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    const char* s2 =
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
    int64_t l1 = (int64_t)strlen(s1);
    int64_t l2 = (int64_t)strlen(s2);
    DuxStr* a = duxrt_str_new(s1, l1);
    DuxStr* b = duxrt_str_new(s2, l2);
    DuxStr* c = duxrt_str_concat(a, b);
    check(c->len == (int32_t)(l1 + l2), "concat ll: len wrong");
    const char* cs = duxrt_str_cstr(c);
    check(strncmp(cs, s1, (size_t)l1) == 0, "concat ll: first half wrong");
    check(strncmp(cs + l1, s2, (size_t)l2) == 0, "concat ll: second half wrong");
    duxrt_str_release(a);
    duxrt_str_release(b);
    duxrt_str_release(c);
}

/* ── test_eq ─────────────────────────────────────────────────────────────── */
static void test_eq(void) {
    DuxStr* a = duxrt_str_new("hello", 5);
    DuxStr* b = duxrt_str_new("hello", 5);
    DuxStr* c = duxrt_str_new("world", 5);
    DuxStr* d = duxrt_str_new("hi",    2);
    check(duxrt_str_eq(a, b) == 1,  "eq: same content should be equal");
    check(duxrt_str_eq(a, c) == 0,  "eq: different content should not be equal");
    check(duxrt_str_eq(a, d) == 0,  "eq: different length should not be equal");
    check(duxrt_str_eq(NULL, NULL) == 1, "eq: NULL==NULL");
    check(duxrt_str_eq(a, NULL) == 0,   "eq: str!=NULL");
    duxrt_str_release(a);
    duxrt_str_release(b);
    duxrt_str_release(c);
    duxrt_str_release(d);
}

/* ── test_index ──────────────────────────────────────────────────────────── */
static void test_index(void) {
    DuxStr* s = duxrt_str_new("abcde", 5);
    DuxStr* c0 = duxrt_str_index(s, 0);
    check(strcmp(duxrt_str_cstr(c0), "a") == 0, "index[0] != 'a'");
    DuxStr* c4 = duxrt_str_index(s, 4);
    check(strcmp(duxrt_str_cstr(c4), "e") == 0, "index[4] != 'e'");
    /* negative index */
    DuxStr* cn1 = duxrt_str_index(s, -1);
    check(strcmp(duxrt_str_cstr(cn1), "e") == 0, "index[-1] != 'e'");
    DuxStr* cn5 = duxrt_str_index(s, -5);
    check(strcmp(duxrt_str_cstr(cn5), "a") == 0, "index[-5] != 'a'");
    /* out-of-bounds returns empty */
    DuxStr* oob = duxrt_str_index(s, 100);
    check(oob->len == 0, "index[100] should be empty");
    duxrt_str_release(s);
    duxrt_str_release(c0);
    duxrt_str_release(c4);
    duxrt_str_release(cn1);
    duxrt_str_release(cn5);
    duxrt_str_release(oob);
}

/* ── test_slice ──────────────────────────────────────────────────────────── */
static void test_slice(void) {
    DuxStr* s  = duxrt_str_new("hello world", 11);
    DuxStr* sl = duxrt_str_slice(s, 6, 11);
    check(sl->len == 5, "slice: len wrong");
    check(strcmp(duxrt_str_cstr(sl), "world") == 0, "slice: content wrong");
    DuxStr* empty = duxrt_str_slice(s, 5, 5);
    check(empty->len == 0, "slice empty: len wrong");
    duxrt_str_release(s);
    duxrt_str_release(sl);
    duxrt_str_release(empty);
}

/* ── test_from_int ───────────────────────────────────────────────────────── */
static void test_from_int(void) {
    DuxStr* s = duxrt_str_from_int(42);
    check(strcmp(duxrt_str_cstr(s), "42") == 0, "from_int(42) wrong");
    duxrt_str_release(s);
    DuxStr* neg = duxrt_str_from_int(-999);
    check(strcmp(duxrt_str_cstr(neg), "-999") == 0, "from_int(-999) wrong");
    duxrt_str_release(neg);
    DuxStr* z = duxrt_str_from_int(0);
    check(strcmp(duxrt_str_cstr(z), "0") == 0, "from_int(0) wrong");
    duxrt_str_release(z);
}

/* ── test_from_double ────────────────────────────────────────────────────── */
static void test_from_double(void) {
    DuxStr* s = duxrt_str_from_double(3.14);
    check(s->len > 0, "from_double: empty result");
    /* %g may produce "3.14" */
    check(strstr(duxrt_str_cstr(s), "3.14") != NULL, "from_double(3.14): unexpected repr");
    duxrt_str_release(s);
    DuxStr* z = duxrt_str_from_double(0.0);
    check(strcmp(duxrt_str_cstr(z), "0") == 0, "from_double(0.0) wrong");
    duxrt_str_release(z);
}

/* ── test_immortal ───────────────────────────────────────────────────────── */
static void test_immortal(void) {
    MAKE_IMMORTAL(im, "immortal literal");
    check(im->refcount == -1, "immortal: refcount should be -1");
    duxrt_str_retain(im);
    check(im->refcount == -1, "immortal: retain should be no-op");
    duxrt_str_release(im);
    check(im->refcount == -1, "immortal: release should be no-op");
    /* cstr still valid */
    check(strcmp(duxrt_str_cstr(im), "immortal literal") == 0,
          "immortal: cstr wrong");
}

/* ── test_cstr_null_safe ─────────────────────────────────────────────────── */
static void test_cstr_null_safe(void) {
    check(duxrt_str_cstr(NULL) == NULL, "cstr(NULL) should be NULL");
    check(duxrt_str_length(NULL) == 0,  "length(NULL) should be 0");
}

/* ── test_length_via_len ─────────────────────────────────────────────────── */
static void test_len_alias(void) {
    DuxStr* s = duxrt_str_new("abcdef", 6);
    check(duxrt_len(s) == 6, "duxrt_len: wrong result");
    duxrt_str_release(s);
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void) {
    test_new_short();
    test_new_long();
    test_retain_refcount();
    test_release_frees();
    test_concat_short_short();
    test_concat_short_long();
    test_concat_long_long();
    test_eq();
    test_index();
    test_slice();
    test_from_int();
    test_from_double();
    test_immortal();
    test_cstr_null_safe();
    test_len_alias();
    printf("test_str: all tests passed\n");
    return 0;
}
