/*
 * str_alloc_bench.c -- micro-benchmark: two-alloc DuxStr vs FAM DuxStr.
 *
 * Build:  cc -O2 -o /tmp/str_alloc_bench benchmarks/str_alloc_bench.c
 * Run:    /tmp/str_alloc_bench
 *
 * We allocate BATCH strings, sum their lengths (forces real data access),
 * then free them. Repeating REPS times gives a stable measurement while
 * preventing the compiler from collapsing alloc+free into a no-op.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BATCH  10000    /* strings allocated before any are freed */
#define REPS   200      /* repetitions */
#define N      ((long)(BATCH) * (REPS))   /* total alloc/free count */

/* ── old: header + separately-malloc'd data ─────────────────────────────── */

typedef struct OldStr {
    int32_t refcount;
    int64_t len;
    char*   data;
} OldStr;

__attribute__((noinline))
static OldStr* old_new(const char* src, int64_t n) {
    OldStr* s = (OldStr*)malloc(sizeof(OldStr));
    s->refcount = 1;
    s->len      = n;
    s->data     = (char*)malloc((size_t)(n + 1));
    memcpy(s->data, src, (size_t)(n + 1));
    return s;
}

__attribute__((noinline))
static void old_free(OldStr* s) {
    free(s->data);
    free(s);
}

/* ── new: single allocation with FAM ────────────────────────────────────── */

typedef struct NewStr {
    int32_t refcount;
    int64_t len;
    char    data[];
} NewStr;

__attribute__((noinline))
static NewStr* new_new(const char* src, int64_t n) {
    NewStr* s = (NewStr*)malloc(sizeof(NewStr) + (size_t)(n + 1));
    s->refcount = 1;
    s->len      = n;
    memcpy(s->data, src, (size_t)(n + 1));
    return s;
}

__attribute__((noinline))
static void new_free(NewStr* s) {
    free(s);
}

/* ── helpers ─────────────────────────────────────────────────────────────── */

static double wallclock(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static char* make_payload(int len) {
    char* p = (char*)malloc((size_t)(len + 1));
    for (int i = 0; i < len; i++) p[i] = (char)('a' + i % 26);
    p[len] = '\0';
    return p;
}

static void bench(const char* tag, const char* src, int64_t n) {
    OldStr** old_ptrs = (OldStr**)malloc(BATCH * sizeof(OldStr*));
    NewStr** new_ptrs = (NewStr**)malloc(BATCH * sizeof(NewStr*));

    /* warm-up */
    for (int i = 0; i < 500; i++) { old_free(old_new(src, n)); }
    for (int i = 0; i < 500; i++) { new_free(new_new(src, n)); }

    /* ── two-alloc ── */
    volatile int64_t sink = 0;
    double t0 = wallclock();
    for (int r = 0; r < REPS; r++) {
        for (int i = 0; i < BATCH; i++) old_ptrs[i] = old_new(src, n);
        for (int i = 0; i < BATCH; i++) sink += old_ptrs[i]->len;
        for (int i = 0; i < BATCH; i++) old_free(old_ptrs[i]);
    }
    double old_t = wallclock() - t0;

    /* ── fam-alloc ── */
    sink = 0;
    t0 = wallclock();
    for (int r = 0; r < REPS; r++) {
        for (int i = 0; i < BATCH; i++) new_ptrs[i] = new_new(src, n);
        for (int i = 0; i < BATCH; i++) sink += new_ptrs[i]->len;
        for (int i = 0; i < BATCH; i++) new_free(new_ptrs[i]);
    }
    double new_t = wallclock() - t0;

    printf("  %s bytes:\n", tag);
    printf("    two-alloc : %6.3f s  (%5.1f M ops/s)\n",
           old_t, (double)N / old_t / 1e6);
    printf("    fam-alloc : %6.3f s  (%5.1f M ops/s)   %.1fx\n\n",
           new_t, (double)N / new_t / 1e6, old_t / new_t);

    (void)sink;
    free(old_ptrs);
    free(new_ptrs);
}

int main(void) {
    printf("DuxStr alloc benchmark  (%ld total alloc/free per variant)\n\n", N);

    char* s8   = make_payload(8);
    char* s40  = make_payload(40);
    char* s200 = make_payload(200);

    bench("  8", s8,   8);
    bench(" 40", s40,  40);
    bench("200", s200, 200);

    free(s8); free(s40); free(s200);
    return 0;
}
