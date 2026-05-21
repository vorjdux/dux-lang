#include "duxrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Internal accessor — avoids function-call overhead inside str.c */
static inline const char* sdata(const DuxStr* s) {
    return s->ext ? s->ext : s->data;
}

DuxStr* duxrt_str_new(const char* src, int64_t len) {
    if (len < 0 || len > INT32_MAX) {
        fputs("duxrt: string length exceeds 2 GiB limit\n", stderr); abort();
    }
    DuxStr* s;
    if (len <= DUXSTR_INLINE_MAX) {
        /* Short path: single allocation, data embedded in FAM. */
        s = (DuxStr*)malloc(sizeof(DuxStr) + (size_t)(len + 1));
        if (!s) { fputs("duxrt: out of memory\n", stderr); abort(); }
        s->ext = NULL;
        if (src) memcpy(s->data, src, (size_t)len);
        s->data[len] = '\0';
    } else {
        /* Long path: header only, data in a separate heap buffer. */
        s = (DuxStr*)malloc(sizeof(DuxStr));
        if (!s) { fputs("duxrt: out of memory\n", stderr); abort(); }
        s->ext = (char*)malloc((size_t)(len + 1));
        if (!s->ext) { fputs("duxrt: out of memory\n", stderr); abort(); }
        if (src) memcpy(s->ext, src, (size_t)len);
        s->ext[len] = '\0';
    }
    s->refcount = 1;
    s->len      = (int32_t)len;   /* int32_t field; caller ensures len <= INT32_MAX */
    return s;
}

DuxStr* duxrt_str_retain(DuxStr* s) {
    if (s && s->refcount != -1) s->refcount++;
    return s;
}

void duxrt_str_release(DuxStr* s) {
    if (!s || s->refcount == -1) return;   /* null or immortal */
    if (--s->refcount == 0) {
        if (s->ext) free(s->ext);          /* long strings: free external buffer */
        free(s);                            /* always: free the header */
    }
}

const char* duxrt_str_cstr(DuxStr* s) {
    return s ? sdata(s) : NULL;
}

DuxStr* duxrt_str_concat(DuxStr* a, DuxStr* b) {
    const char* pa = a ? sdata(a) : "";
    const char* pb = b ? sdata(b) : "";
    int64_t la = a ? a->len : 0;
    int64_t lb = b ? b->len : 0;
    DuxStr* out = duxrt_str_new(NULL, la + lb);
    char* dst = out->ext ? out->ext : out->data;
    memcpy(dst,      pa, (size_t)la);
    memcpy(dst + la, pb, (size_t)lb);
    dst[la + lb] = '\0';
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
    return s ? (int64_t)s->len : 0;
}

DuxStr* duxrt_str_index(DuxStr* s, int64_t i) {
    if (!s) return duxrt_str_new("", 0);
    if (i < 0) i += s->len;
    if (i < 0 || i >= s->len) return duxrt_str_new("", 0);
    return duxrt_str_new(sdata(s) + i, 1);
}

DuxStr* duxrt_str_slice(DuxStr* s, int64_t start, int64_t end) {
    if (!s) return duxrt_str_new("", 0);
    if (start < 0)    start = 0;
    if (end > s->len) end   = s->len;
    if (start >= end) return duxrt_str_new("", 0);
    return duxrt_str_new(sdata(s) + start, end - start);
}

int duxrt_str_eq(DuxStr* a, DuxStr* b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    return memcmp(sdata(a), sdata(b), (size_t)a->len) == 0;
}

int64_t duxrt_len(DuxStr* s) {
    return s ? (int64_t)s->len : 0;
}
