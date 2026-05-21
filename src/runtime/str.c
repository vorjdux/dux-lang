#include "duxrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DuxStr* duxrt_str_new(const char* data, int64_t len) {
    /* Single allocation: struct header + embedded char data[len+1] */
    DuxStr* s = (DuxStr*)malloc(sizeof(DuxStr) + (size_t)(len + 1));
    if (!s) { fputs("duxrt: out of memory\n", stderr); abort(); }
    s->refcount = 1;
    s->len      = len;
    if (data) memcpy(s->data, data, (size_t)len);
    s->data[len] = '\0';
    return s;
}

DuxStr* duxrt_str_retain(DuxStr* s) {
    if (s && s->refcount != -1) s->refcount++;
    return s;
}

void duxrt_str_release(DuxStr* s) {
    if (!s || s->refcount == -1) return;   /* null or immortal */
    if (--s->refcount == 0) free(s);       /* data is embedded — single free */
}

const char* duxrt_str_cstr(DuxStr* s) {
    return s ? s->data : NULL;
}

DuxStr* duxrt_str_concat(DuxStr* a, DuxStr* b) {
    const char* pa = a ? a->data : "";
    const char* pb = b ? b->data : "";
    int64_t la = a ? a->len : 0;
    int64_t lb = b ? b->len : 0;
    DuxStr* out = duxrt_str_new(NULL, la + lb);
    memcpy(out->data,      pa, (size_t)la);
    memcpy(out->data + la, pb, (size_t)lb);
    out->data[la + lb] = '\0';
    return out;
}

DuxStr* duxrt_str_from_int(int64_t v) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%ld", (long)v);
    return duxrt_str_new(buf, (int64_t)(n > 0 ? n : 0));
}

DuxStr* duxrt_str_from_double(double v) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%g", v);
    return duxrt_str_new(buf, (int64_t)(n > 0 ? n : 0));
}

int64_t duxrt_str_length(DuxStr* s) {
    return s ? s->len : 0;
}

DuxStr* duxrt_str_index(DuxStr* s, int64_t i) {
    if (!s) return duxrt_str_new("", 0);
    if (i < 0) i += s->len;
    if (i < 0 || i >= s->len) return duxrt_str_new("", 0);
    return duxrt_str_new(s->data + i, 1);
}

DuxStr* duxrt_str_slice(DuxStr* s, int64_t start, int64_t end) {
    if (!s) return duxrt_str_new("", 0);
    if (start < 0)      start = 0;
    if (end > s->len)   end   = s->len;
    if (start >= end)   return duxrt_str_new("", 0);
    return duxrt_str_new(s->data + start, end - start);
}

int duxrt_str_eq(DuxStr* a, DuxStr* b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    return memcmp(a->data, b->data, (size_t)a->len) == 0;
}

int64_t duxrt_len(DuxStr* s) {
    return s ? s->len : 0;
}
