#include "duxrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* duxrt_str_new(const char* data, int64_t len) {
    char* s = (char*)duxrt_str_arena_alloc((size_t)(len + 1));
    if (data) memcpy(s, data, (size_t)len);
    s[len] = '\0';
    return s;
}

char* duxrt_str_concat(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a), lb = strlen(b);
    char* out = (char*)duxrt_str_arena_alloc(la + lb + 1);
    memcpy(out, a, la);
    memcpy(out + la, b, lb + 1);
    return out;
}

char* duxrt_str_from_int(int64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", (long)v);
    return duxrt_str_new(buf, (int64_t)strlen(buf));
}

char* duxrt_str_from_double(double v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", v);
    return duxrt_str_new(buf, (int64_t)strlen(buf));
}

int64_t duxrt_str_length(const char* s) {
    return s ? (int64_t)strlen(s) : 0;
}

char* duxrt_str_index(const char* s, int64_t i) {
    if (!s) return duxrt_str_new("", 0);
    int64_t n = (int64_t)strlen(s);
    if (i < 0) i += n;
    if (i < 0 || i >= n) return duxrt_str_new("", 0);
    return duxrt_str_new(s + i, 1);
}

char* duxrt_str_slice(const char* s, int64_t start, int64_t end) {
    if (!s) return duxrt_str_new("", 0);
    int64_t n = (int64_t)strlen(s);
    if (start < 0) start = 0;
    if (end > n)   end   = n;
    if (start >= end) return duxrt_str_new("", 0);
    return duxrt_str_new(s + start, end - start);
}

int duxrt_str_eq(const char* a, const char* b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

int64_t duxrt_len(const void* obj) {
    /* For strings; lists pass their len field via duxrt_list_len */
    return obj ? (int64_t)strlen((const char*)obj) : 0;
}
