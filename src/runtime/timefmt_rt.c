#define _GNU_SOURCE
#include "duxrt.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>

/*
 * duxrt_timefmt_parse — parse a date/time string using strptime.
 * Returns the Unix timestamp on success, or -1 on parse failure.
 */
int64_t duxrt_timefmt_parse(DuxStr* s, DuxStr* fmt) {
    if (!s || !fmt) return -1;
    const char* str_c = duxrt_str_cstr(s);
    const char* fmt_c = duxrt_str_cstr(fmt);
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_isdst = -1;  /* let mktime determine DST */
    char* end = strptime(str_c, fmt_c, &tm);
    if (!end) return -1LL;
    time_t ts = timegm(&tm);   /* UTC, not local time */
    if (ts == (time_t)-1) return -1LL;
    return (int64_t)ts;
}
