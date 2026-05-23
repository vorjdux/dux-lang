/*
 * duxrt — random number generation runtime
 *
 * Provides PRNG functions for the math.random stdlib module.
 * Uses the OS random source (getrandom/urandom) for seeding
 * and a simple LCG/xoshiro256** for fast generation.
 */
#define _GNU_SOURCE
#include "duxrt.h"
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

/* ── xoshiro256** PRNG ───────────────────────────────────────────────────── */

static uint64_t rng_state[4] = {
    0x6c62272e07bb0142ULL,
    0x62b821756295c58dULL,
    0x51503d7f8c6f5a34ULL,
    0x92ba0b2f7823feabULL
};

static inline uint64_t rotl64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t xoshiro256ss_next(void) {
    uint64_t* s = rng_state;
    uint64_t result = rotl64(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1];
    s[1] ^= s[2]; s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl64(s[3], 45);
    return result;
}

/* ── Seeding ─────────────────────────────────────────────────────────────── */

void duxrt_random_seed(int64_t seed) {
    /* Use splitmix64 to initialise xoshiro state from one seed value */
    uint64_t z = (uint64_t)seed;
    for (int i = 0; i < 4; ++i) {
        z += 0x9e3779b97f4a7c15ULL;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        rng_state[i] = z ^ (z >> 31);
    }
}

/* ── random() — uniform double in [0, 1) ─────────────────────────────────── */

double duxrt_random(void) {
    uint64_t r = xoshiro256ss_next();
    /* Top 53 bits → mantissa of a double in [1, 2) then subtract 1 */
    union { uint64_t u; double d; } c;
    c.u = (UINT64_C(0x3FF) << 52) | (r >> 11);
    return c.d - 1.0;
}

/* ── random_int(lo, hi) — uniform int in [lo, hi] ────────────────────────── */

int64_t duxrt_random_int(int64_t lo, int64_t hi) {
    if (lo >= hi) return lo;
    int64_t range = hi - lo + 1;
    uint64_t r = xoshiro256ss_next();
    return lo + (int64_t)(r % (uint64_t)range);
}

/* ── random_double(lo, hi) — uniform double in [lo, hi) ──────────────────── */

double duxrt_random_double(double lo, double hi) {
    return lo + duxrt_random() * (hi - lo);
}

/* ── random_bool(p) — true with probability p ────────────────────────────── */

int duxrt_random_bool(double p) {
    return duxrt_random() < p ? 1 : 0;
}

/* ── Shuffle a DuxList in place (Fisher-Yates) ───────────────────────────── */

void duxrt_random_shuffle(DuxList* lst) {
    if (!lst || lst->len <= 1) return;
    for (int64_t i = lst->len - 1; i > 0; --i) {
        int64_t j = duxrt_random_int(0, i);
        void* tmp = lst->data[i];
        lst->data[i] = lst->data[j];
        lst->data[j] = tmp;
    }
}
