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
    if (s && atomic_load(&s->refcount) != -1) atomic_fetch_add(&s->refcount, 1);
    return s;
}

void duxrt_str_release(DuxStr* s) {
    if (!s || atomic_load(&s->refcount) == -1) return;   /* null or immortal */
    if (atomic_fetch_sub(&s->refcount, 1) == 1) {
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

DuxStr* duxrt_str_join_list(DuxList* parts, int64_t count) {
    if (!parts || count <= 0) return duxrt_str_new("", 0);
    /* First pass: compute total length */
    int64_t total = 0;
    for (int64_t i = 0; i < count; i++) {
        DuxStr* s = (DuxStr*)duxrt_list_get(parts, i);
        if (s) total += (int64_t)s->len;
    }
    /* Single allocation for the result */
    DuxStr* out = duxrt_str_new(NULL, total);
    char* dst = out->ext ? out->ext : out->data;
    /* Second pass: copy each part */
    for (int64_t i = 0; i < count; i++) {
        DuxStr* s = (DuxStr*)duxrt_list_get(parts, i);
        if (!s) continue;
        const char* src = s->ext ? s->ext : s->data;
        memcpy(dst, src, (size_t)s->len);
        dst += s->len;
    }
    *dst = '\0';
    return out;
}

/* ── StringBuffer — pre-allocated growing char buffer ─────────────────── */

#define STRBUF_INIT_CAP 64

static void strbuf_grow(DuxStrBuf* b, int64_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    int64_t nc = b->cap ? b->cap : STRBUF_INIT_CAP;
    while (nc < b->len + extra + 1) nc *= 2;
    b->data = (char*)realloc(b->data, (size_t)nc);
    if (!b->data) { fputs("duxrt: out of memory\n", stderr); abort(); }
    b->cap = nc;
}

DuxStrBuf* duxrt_strbuf_new(void) {
    DuxStrBuf* b = (DuxStrBuf*)malloc(sizeof(DuxStrBuf));
    if (!b) { fputs("duxrt: out of memory\n", stderr); abort(); }
    b->data = (char*)malloc(STRBUF_INIT_CAP);
    if (!b->data) { fputs("duxrt: out of memory\n", stderr); abort(); }
    b->data[0] = '\0';
    b->len = 0;
    b->cap = STRBUF_INIT_CAP;
    return b;
}

void duxrt_strbuf_append_str(DuxStrBuf* b, DuxStr* s) {
    if (!b || !s || s->len == 0) return;
    strbuf_grow(b, (int64_t)s->len);
    const char* src = s->ext ? s->ext : s->data;
    memcpy(b->data + b->len, src, (size_t)s->len);
    b->len += s->len;
    b->data[b->len] = '\0';
}

DuxStr* duxrt_strbuf_build(DuxStrBuf* b) {
    if (!b) return duxrt_str_new("", 0);
    return duxrt_str_new(b->data, b->len);
}

void duxrt_strbuf_free(DuxStrBuf* b) {
    if (!b) return;
    free(b->data);
    free(b);
}
