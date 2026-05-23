#include "duxrt.h"
#include <math.h>
#include <stdlib.h>
#include <stdint.h>

/* ── Basic arithmetic ─────────────────────────────────────────────────────── */
double  duxrt_math_sqrt(double x)            { return sqrt(x); }
double  duxrt_math_pow(double x, double y)   { return pow(x, y); }
double  duxrt_math_abs_d(double x)           { return fabs(x); }
int64_t duxrt_math_abs_i(int64_t x) {
    if (x == INT64_MIN) return INT64_MAX;
    return x < 0 ? -x : x;
}
double  duxrt_math_min_d(double a, double b) { return a < b ? a : b; }
double  duxrt_math_max_d(double a, double b) { return a > b ? a : b; }
int64_t duxrt_math_min_i(int64_t a, int64_t b) { return a < b ? a : b; }
int64_t duxrt_math_max_i(int64_t a, int64_t b) { return a > b ? a : b; }

/* ── Trigonometry ─────────────────────────────────────────────────────────── */
double  duxrt_math_sin(double x)             { return sin(x); }
double  duxrt_math_cos(double x)             { return cos(x); }
double  duxrt_math_tan(double x)             { return tan(x); }
double  duxrt_math_asin(double x)            { return asin(x); }
double  duxrt_math_acos(double x)            { return acos(x); }
double  duxrt_math_atan(double x)            { return atan(x); }
double  duxrt_math_atan2(double y, double x) { return atan2(y, x); }

/* ── Exponential / logarithm ─────────────────────────────────────────────── */
double  duxrt_math_exp(double x)             { return exp(x); }
double  duxrt_math_exp2(double x)            { return exp2(x); }
double  duxrt_math_log(double x)             { return log(x); }
double  duxrt_math_log2(double x)            { return log2(x); }
double  duxrt_math_log10(double x)           { return log10(x); }

/* ── Rounding ─────────────────────────────────────────────────────────────── */
double  duxrt_math_floor(double x)           { return floor(x); }
double  duxrt_math_ceil(double x)            { return ceil(x); }
double  duxrt_math_round(double x)           { return round(x); }
double  duxrt_math_trunc(double x)           { return trunc(x); }

/* ── Floating-point utilities ─────────────────────────────────────────────── */
double  duxrt_math_fmod(double x, double y)  { return fmod(x, y); }
double  duxrt_math_hypot(double x, double y) { return hypot(x, y); }

/* ── Clamp ────────────────────────────────────────────────────────────────── */
double  duxrt_math_clamp_d(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
int64_t duxrt_math_clamp_i(int64_t v, int64_t lo, int64_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* ── Sign ─────────────────────────────────────────────────────────────────── */
int32_t duxrt_math_sign_d(double x) {
    if (x > 0.0) return  1;
    if (x < 0.0) return -1;
    return 0;
}
int32_t duxrt_math_sign_i(int64_t x) {
    if (x > 0) return  1;
    if (x < 0) return -1;
    return 0;
}

/* ── Classification ───────────────────────────────────────────────────────── */
int32_t duxrt_math_is_nan(double x) { return isnan(x) ? 1 : 0; }
int32_t duxrt_math_is_inf(double x) { return isinf(x) ? 1 : 0; }
