/*
 * duxrt — time/clock runtime
 *
 * Provides time functions for the time.clock and time.date stdlib modules.
 * All string-returning functions return a new DuxStr* (refcount=1).
 */
#define _POSIX_C_SOURCE 200809L
#include "duxrt.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Monotonic clock (nanoseconds since some arbitrary epoch) ─────────────── */

int64_t duxrt_clock_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

/* ── Wall-clock time (Unix epoch in seconds, double for sub-second) ───────── */

double duxrt_clock_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ── Sleep for given milliseconds ───────────────────────────────────────── */

void duxrt_clock_sleep_ms(int64_t ms) {
    struct timespec req;
    req.tv_sec  = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000LL;
    nanosleep(&req, NULL);
}

/* ── Current Unix timestamp in seconds (integer) ─────────────────────────── */

int64_t duxrt_clock_unix(void) {
    return (int64_t)time(NULL);
}

/* ── Format current time as ISO-8601 string ──────────────────────────────── */

DuxStr* duxrt_clock_now_str(void) {
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_info);
    return duxrt_str_new(buf, (int64_t)strlen(buf));
}

/* ── Date components from Unix timestamp ─────────────────────────────────── */

int64_t duxrt_date_year(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    return (int64_t)(tm_info.tm_year + 1900);
}

int64_t duxrt_date_month(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    return (int64_t)(tm_info.tm_mon + 1);
}

int64_t duxrt_date_day(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    return (int64_t)tm_info.tm_mday;
}

int64_t duxrt_date_hour(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    return (int64_t)tm_info.tm_hour;
}

int64_t duxrt_date_minute(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    return (int64_t)tm_info.tm_min;
}

int64_t duxrt_date_second(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    return (int64_t)tm_info.tm_sec;
}

/* ── Format timestamp with custom format ────────────────────────────────── */

DuxStr* duxrt_date_format(int64_t unix_ts, DuxStr* fmt) {
    const char* f = duxrt_str_cstr(fmt);
    if (!f) f = "%Y-%m-%d";
    time_t t = (time_t)unix_ts;
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    char buf[256];
    strftime(buf, sizeof(buf), f, &tm_info);
    return duxrt_str_new(buf, (int64_t)strlen(buf));
}
