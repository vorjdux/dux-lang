/*
 * random_rt.c — PRNG runtime for Dux stdlib math.random
 * Uses xoshiro256** algorithm (public domain).
 */
#include "duxrt.h"
#include <stdint.h>
#include <time.h>
#include <string.h>

static uint64_t s[4] = {
    0x123456789abcdef0ULL,
    0xdeadbeef12345678ULL,
    0xfeedface87654321ULL,
    0xc0ffee9988776655ULL
};

static inline uint64_t rotl64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t xoshiro_next(void) {
    uint64_t r = rotl64(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3]  = rotl64(s[3], 45);
    return r;
}

void duxrt_random_seed(int64_t seed) {
    s[0] = (uint64_t)seed;
    s[1] = (uint64_t)seed ^ 0xdeadbeefULL;
    s[2] = (uint64_t)seed * 6364136223846793005ULL;
    s[3] = (uint64_t)seed ^ 0xcafebabeULL;
    /* Warm up */
    for (int i = 0; i < 20; i++) xoshiro_next();
}

/* Returns a double in [0, 1) */
double duxrt_random(void) {
    return (double)(xoshiro_next() >> 11) / (double)(UINT64_C(1) << 53);
}

/* Returns an integer in [lo, hi) */
int64_t duxrt_random_int(int64_t lo, int64_t hi) {
    if (lo >= hi) return lo;
    uint64_t range = (uint64_t)(hi - lo);
    return lo + (int64_t)(xoshiro_next() % range);
}

/* Returns a double in [lo, hi) */
double duxrt_random_double(double lo, double hi) {
    return lo + duxrt_random() * (hi - lo);
}

/* Returns 1 with probability p, 0 otherwise */
int32_t duxrt_random_bool(double p) {
    return duxrt_random() < p ? 1 : 0;
}

/* Fisher-Yates shuffle in place */
void duxrt_random_shuffle(DuxList* list) {
    if (!list) return;
    int64_t n = duxrt_list_len(list);
    for (int64_t i = n - 1; i > 0; i--) {
        int64_t j = (int64_t)(xoshiro_next() % (uint64_t)(i + 1));
        void* tmp = duxrt_list_get(list, i);
        duxrt_list_set(list, i, duxrt_list_get(list, j));
        duxrt_list_set(list, j, tmp);
    }
}
