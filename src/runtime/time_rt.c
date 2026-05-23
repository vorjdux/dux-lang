/*
 * time_rt.c — clock and date runtime for Dux stdlib time.clock / time.date
 */
#include "duxrt.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

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
    struct timespec rem;
    rem.tv_sec  = ms / 1000;
    rem.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&rem, &rem) != 0) {
        if (errno != EINTR) break;
    }
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

int64_t duxrt_clock_unix_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000LL);
}

int64_t duxrt_clock_mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

int64_t duxrt_date_weekday(int64_t unix_ts) {
    time_t t = (time_t)unix_ts;
    struct tm tm_info;
    gmtime_r(&t, &tm_info);
    return (int64_t)tm_info.tm_wday;  /* 0=Sun..6=Sat */
}

int64_t duxrt_date_to_unix(int64_t y, int64_t mo, int64_t d,
                            int64_t h, int64_t mi, int64_t s) {
    struct tm tm_info = {0};
    tm_info.tm_year = (int)(y - 1900);
    tm_info.tm_mon  = (int)(mo - 1);
    tm_info.tm_mday = (int)d;
    tm_info.tm_hour = (int)h;
    tm_info.tm_min  = (int)mi;
    tm_info.tm_sec  = (int)s;
    tm_info.tm_isdst = -1;
    time_t t = mktime(&tm_info);
    return (int64_t)t;
}
