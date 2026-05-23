/*
 * time_rt.c — clock and date runtime for Dux stdlib time.clock / time.date
 */
#include "duxrt.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

int64_t duxrt_clock_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

double duxrt_clock_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}

void duxrt_clock_sleep_ms(int64_t ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int64_t duxrt_clock_unix(void) {
    return (int64_t)time(NULL);
}

DuxStr* duxrt_clock_now_str(void) {
    time_t t = time(NULL);
    char buf[64];
    struct tm* tm_info = localtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return duxrt_str_new(buf, (int64_t)strlen(buf));
}

int64_t duxrt_date_year(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm* tm_info = localtime(&t);
    return (int64_t)(1900 + tm_info->tm_year);
}

int64_t duxrt_date_month(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm* tm_info = localtime(&t);
    return (int64_t)(1 + tm_info->tm_mon);
}

int64_t duxrt_date_day(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm* tm_info = localtime(&t);
    return (int64_t)tm_info->tm_mday;
}

int64_t duxrt_date_hour(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm* tm_info = localtime(&t);
    return (int64_t)tm_info->tm_hour;
}

int64_t duxrt_date_minute(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm* tm_info = localtime(&t);
    return (int64_t)tm_info->tm_min;
}

int64_t duxrt_date_second(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm* tm_info = localtime(&t);
    return (int64_t)tm_info->tm_sec;
}

DuxStr* duxrt_date_format(int64_t unix_ts, DuxStr* fmt) {
    time_t t = (time_t)unix_ts;
    struct tm* tm_info = localtime(&t);
    char buf[256];
    strftime(buf, sizeof(buf), duxrt_str_cstr(fmt), tm_info);
    return duxrt_str_new(buf, (int64_t)strlen(buf));
}

DuxStr* duxrt_date_iso(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    char buf[32];
    struct tm* tm_info = localtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%d", tm_info);
    return duxrt_str_new(buf, (int64_t)strlen(buf));
}
