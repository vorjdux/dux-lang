/*
 * str_alloc_bench.c -- micro-benchmark comparing DuxStr allocation strategies.
 *
 * Three variants:
 *   two-alloc  (original)  header + separate heap data
 *   fam-only               single alloc, data always inline via FAM
 *   hybrid                 FAM for short strings, two-alloc for long strings
 *
 * Build:  cc -O2 -o /tmp/str_alloc_bench benchmarks/str_alloc_bench.c
 * Run:    /tmp/str_alloc_bench
 *
 * We allocate BATCH strings at once, read all their data pointers (prevents
 * the compiler folding alloc+free into a no-op), then free them all, and
 * repeat REPS times.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BATCH             10000
#define REPS              200
#define TOTAL_OPS         ((long)(BATCH) * (REPS))
#define HYBRID_INLINE_MAX 63    /* must match DUXSTR_INLINE_MAX in duxrt.h */

/* ── old: header + separately-malloc'd data ─────────────────────────────── */

typedef struct TwoStr {
    int32_t refcount;
    int64_t len;
    char*   data;
} TwoStr;

__attribute__((noinline))
static TwoStr* two_new(const char* src, int64_t n) {
    TwoStr* s = (TwoStr*)malloc(sizeof(TwoStr));
    s->refcount = 1; s->len = n;
    s->data = (char*)malloc((size_t)(n + 1));
    memcpy(s->data, src, (size_t)(n + 1));
    return s;
}
__attribute__((noinline))
static void two_free(TwoStr* s) { free(s->data); free(s); }

/* ── fam-only: always single alloc with inline data ─────────────────────── */

typedef struct FamStr {
    int32_t refcount;
    int64_t len;
    char    data[];
} FamStr;

__attribute__((noinline))
static FamStr* fam_new(const char* src, int64_t n) {
    FamStr* s = (FamStr*)malloc(sizeof(FamStr) + (size_t)(n + 1));
    s->refcount = 1; s->len = n;
    memcpy(s->data, src, (size_t)(n + 1));
    return s;
}
__attribute__((noinline))
static void fam_free(FamStr* s) { free(s); }

/* ── hybrid: FAM for short, two-alloc for long ───────────────────────────── */

typedef struct HybStr {
    int32_t refcount;
    int64_t len;
    char*   ext;     /* NULL = inline (short), non-NULL = heap (long) */
    char    data[];  /* FAM — only valid when ext == NULL */
} HybStr;

__attribute__((noinline))
static HybStr* hyb_new(const char* src, int64_t n) {
    HybStr* s;
    if (n <= HYBRID_INLINE_MAX) {
        s = (HybStr*)malloc(sizeof(HybStr) + (size_t)(n + 1));
        s->ext = NULL;
        memcpy(s->data, src, (size_t)(n + 1));
    } else {
        s = (HybStr*)malloc(sizeof(HybStr));
        s->ext = (char*)malloc((size_t)(n + 1));
        memcpy(s->ext, src, (size_t)(n + 1));
    }
    s->refcount = 1; s->len = n;
    return s;
}
__attribute__((noinline))
static void hyb_free(HybStr* s) { if (s->ext) free(s->ext); free(s); }
static inline const char* hyb_data(const HybStr* s) { return s->ext ? s->ext : s->data; }

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

static void bench_all(int str_len, const char* src) {
    void**  ptrs   = (void**)malloc(BATCH * sizeof(void*));
    volatile int64_t sink = 0;
    double t0, elapsed;
    const char* path = str_len <= HYBRID_INLINE_MAX ? "inline" : "ext";

    /* warm-up */
    for (int i = 0; i < 1000; i++) two_free(two_new(src, str_len));
    for (int i = 0; i < 1000; i++) fam_free(fam_new(src, str_len));
    for (int i = 0; i < 1000; i++) hyb_free(hyb_new(src, str_len));

    /* two-alloc */
    t0 = wallclock();
    for (int r = 0; r < REPS; r++) {
        for (int i = 0; i < BATCH; i++) ptrs[i] = two_new(src, str_len);
        for (int i = 0; i < BATCH; i++) sink += ((TwoStr*)ptrs[i])->len;
        for (int i = 0; i < BATCH; i++) two_free((TwoStr*)ptrs[i]);
    }
    elapsed = wallclock() - t0;
    double two_t = elapsed;
    printf("  two-alloc        %6.3f s  %5.1f M/s\n",
           elapsed, (double)TOTAL_OPS / elapsed / 1e6);

    /* fam-only */
    sink = 0;
    t0 = wallclock();
    for (int r = 0; r < REPS; r++) {
        for (int i = 0; i < BATCH; i++) ptrs[i] = fam_new(src, str_len);
        for (int i = 0; i < BATCH; i++) sink += ((FamStr*)ptrs[i])->len;
        for (int i = 0; i < BATCH; i++) fam_free((FamStr*)ptrs[i]);
    }
    elapsed = wallclock() - t0;
    printf("  fam-only         %6.3f s  %5.1f M/s   %+.0f%%\n",
           elapsed, (double)TOTAL_OPS / elapsed / 1e6,
           (two_t - elapsed) / elapsed * 100.0);

    /* hybrid */
    sink = 0;
    t0 = wallclock();
    for (int r = 0; r < REPS; r++) {
        for (int i = 0; i < BATCH; i++) ptrs[i] = hyb_new(src, str_len);
        for (int i = 0; i < BATCH; i++) sink += (int64_t)strlen(hyb_data((HybStr*)ptrs[i]));
        for (int i = 0; i < BATCH; i++) hyb_free((HybStr*)ptrs[i]);
    }
    elapsed = wallclock() - t0;
    printf("  hybrid (%s) %6.3f s  %5.1f M/s   %+.0f%%\n\n",
           path, elapsed, (double)TOTAL_OPS / elapsed / 1e6,
           (two_t - elapsed) / elapsed * 100.0);

    (void)sink;
    free(ptrs);
}

int main(void) {
    printf("DuxStr allocation benchmark  "
           "(%d strings x %d reps = %ld total ops)\n\n",
           BATCH, REPS, TOTAL_OPS);
    printf("  %-16s  %8s  %8s  %8s\n\n", "variant", "time", "M ops/s", "vs two-alloc");

    int lens[] = { 8, 40, 64, 200 };
    for (int k = 0; k < 4; k++) {
        int n = lens[k];
        char* src = make_payload(n);
        printf("String length = %d bytes  "
               "(hybrid threshold = %d):\n", n, HYBRID_INLINE_MAX);
        bench_all(n, src);
        free(src);
    }
    return 0;
}
