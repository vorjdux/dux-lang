#include "duxrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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

/* ── Stack-buffer format helpers (no heap allocation) ───────────────────── */
/* buf must be at least 32 bytes. Returns byte count (excluding NUL). */
int32_t duxrt_fmt_int(int64_t v, char* buf) {
    int n = snprintf(buf, 32, "%ld", (long)v);
    return (int32_t)(n > 0 ? n : 0);
}

/* buf must be at least 64 bytes. Returns byte count (excluding NUL). */
int32_t duxrt_fmt_double(double v, char* buf) {
    int n = snprintf(buf, 64, "%g", v);
    return (int32_t)(n > 0 ? n : 0);
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

/* ── Extended string operations ─────────────────────────────────────────────
 * All functions follow the same ownership model as the rest of str.c:
 *   - functions returning DuxStr* return a newly-allocated value (refcount=1)
 *   - inputs are not consumed (caller still owns them)
 * ─────────────────────────────────────────────────────────────────────────── */

/* Internal: find needle inside haystack, return pointer or NULL. */
static const char* mem_find(const char* h, size_t hlen,
                             const char* n, size_t nlen) {
    if (nlen == 0) return h;
    if (nlen > hlen) return NULL;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        if (memcmp(h + i, n, nlen) == 0) return h + i;
    }
    return NULL;
}

/* Return the ASCII code point of the first byte of s (0 if empty/null). */
int64_t duxrt_str_ord(DuxStr* s) {
    if (!s || s->len == 0) return 0;
    return (int64_t)(unsigned char)sdata(s)[0];
}

/* Return a one-character string for the given ASCII code point.
   Returns "" for values outside 1-127. */
DuxStr* duxrt_str_chr(int64_t code) {
    if (code < 1 || code > 127) return duxrt_str_new("", 0);
    char buf[1] = { (char)(unsigned char)code };
    return duxrt_str_new(buf, 1);
}

/* Lexicographic comparison: -1, 0, or 1. */
int duxrt_str_cmp(DuxStr* a, DuxStr* b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return  1;
    int64_t min_len = a->len < b->len ? a->len : b->len;
    int r = memcmp(sdata(a), sdata(b), (size_t)min_len);
    if (r != 0) return r < 0 ? -1 : 1;
    if (a->len < b->len) return -1;
    if (a->len > b->len) return  1;
    return 0;
}

/* Return 1 if haystack contains needle as a substring, else 0. */
int duxrt_str_contains(DuxStr* haystack, DuxStr* needle) {
    if (!haystack || !needle) return 0;
    if (needle->len == 0) return 1;
    return mem_find(sdata(haystack), (size_t)haystack->len,
                    sdata(needle),   (size_t)needle->len) != NULL ? 1 : 0;
}

/* Return the first index of needle in haystack, or -1 if absent. */
int64_t duxrt_str_find(DuxStr* haystack, DuxStr* needle) {
    if (!haystack || !needle) return -1;
    if (needle->len == 0) return 0;
    const char* h = sdata(haystack);
    const char* p = mem_find(h, (size_t)haystack->len,
                             sdata(needle), (size_t)needle->len);
    return p ? (int64_t)(p - h) : -1;
}

/* Return the last index of needle in haystack, or -1 if absent. */
int64_t duxrt_str_rfind(DuxStr* haystack, DuxStr* needle) {
    if (!haystack || !needle) return -1;
    if (needle->len == 0) return (int64_t)haystack->len;
    if ((int64_t)needle->len > (int64_t)haystack->len) return -1;
    const char* h = sdata(haystack);
    const char* n = sdata(needle);
    int64_t last = -1;
    for (int64_t i = 0; i <= (int64_t)haystack->len - (int64_t)needle->len; i++) {
        if (memcmp(h + i, n, (size_t)needle->len) == 0) last = i;
    }
    return last;
}

/* Return 1 if s starts with prefix, else 0. */
int duxrt_str_starts_with(DuxStr* s, DuxStr* prefix) {
    if (!s || !prefix) return 0;
    if (prefix->len == 0) return 1;
    if ((int64_t)prefix->len > (int64_t)s->len) return 0;
    return memcmp(sdata(s), sdata(prefix), (size_t)prefix->len) == 0 ? 1 : 0;
}

/* Return 1 if s ends with suffix, else 0. */
int duxrt_str_ends_with(DuxStr* s, DuxStr* suffix) {
    if (!s || !suffix) return 0;
    if (suffix->len == 0) return 1;
    if ((int64_t)suffix->len > (int64_t)s->len) return 0;
    return memcmp(sdata(s) + s->len - suffix->len,
                  sdata(suffix), (size_t)suffix->len) == 0 ? 1 : 0;
}

/* Replace the first occurrence of `from` with `to`.  Returns a new string
   (or a retained copy if `from` is not found). */
DuxStr* duxrt_str_replace(DuxStr* s, DuxStr* from, DuxStr* to) {
    if (!s || !from || from->len == 0) {
        return s ? duxrt_str_retain(s) : duxrt_str_new("", 0);
    }
    int64_t idx = duxrt_str_find(s, from);
    if (idx < 0) return duxrt_str_retain(s);
    const char* sp   = sdata(s);
    const char* tp   = to ? sdata(to) : "";
    int64_t     tlen = to ? (int64_t)to->len : 0;
    int64_t     total = idx + tlen + ((int64_t)s->len - idx - (int64_t)from->len);
    DuxStr* out = duxrt_str_new(NULL, total);
    char* dst = out->ext ? out->ext : out->data;
    memcpy(dst,             sp,      (size_t)idx);
    memcpy(dst + idx,       tp,      (size_t)tlen);
    memcpy(dst + idx + tlen, sp + idx + from->len,
           (size_t)((int64_t)s->len - idx - (int64_t)from->len));
    dst[total] = '\0';
    return out;
}

/* Replace every occurrence of `from` with `to`.  O(n). */
DuxStr* duxrt_str_replace_all(DuxStr* s, DuxStr* from, DuxStr* to) {
    if (!s || !from || from->len == 0) {
        return s ? duxrt_str_retain(s) : duxrt_str_new("", 0);
    }
    DuxStrBuf* buf = duxrt_strbuf_new();
    const char* sp   = sdata(s);
    const char* fp   = sdata(from);
    const char* tp   = to ? sdata(to) : "";
    int64_t     tlen = to ? (int64_t)to->len : 0;
    int64_t i = 0;
    int64_t limit = (int64_t)s->len - (int64_t)from->len;
    while (i <= limit) {
        if (memcmp(sp + i, fp, (size_t)from->len) == 0) {
            DuxStr* piece = duxrt_str_new(tp, tlen);
            duxrt_strbuf_append_str(buf, piece);
            duxrt_str_release(piece);
            i += (int64_t)from->len;
        } else {
            DuxStr* ch = duxrt_str_new(sp + i, 1);
            duxrt_strbuf_append_str(buf, ch);
            duxrt_str_release(ch);
            i++;
        }
    }
    /* append any remaining tail characters */
    if (i < (int64_t)s->len) {
        DuxStr* tail = duxrt_str_new(sp + i, (int64_t)s->len - i);
        duxrt_strbuf_append_str(buf, tail);
        duxrt_str_release(tail);
    }
    DuxStr* result = duxrt_strbuf_build(buf);
    duxrt_strbuf_free(buf);
    return result;
}

/* Return an uppercased copy of s (ASCII only). */
DuxStr* duxrt_str_to_upper(DuxStr* s) {
    if (!s || s->len == 0) return duxrt_str_new("", 0);
    DuxStr* out = duxrt_str_new(sdata(s), (int64_t)s->len);
    char* dst = out->ext ? out->ext : out->data;
    for (int32_t i = 0; i < out->len; i++) {
        if (dst[i] >= 'a' && dst[i] <= 'z') dst[i] = (char)(dst[i] - 32);
    }
    return out;
}

/* Return a lowercased copy of s (ASCII only). */
DuxStr* duxrt_str_to_lower(DuxStr* s) {
    if (!s || s->len == 0) return duxrt_str_new("", 0);
    DuxStr* out = duxrt_str_new(sdata(s), (int64_t)s->len);
    char* dst = out->ext ? out->ext : out->data;
    for (int32_t i = 0; i < out->len; i++) {
        if (dst[i] >= 'A' && dst[i] <= 'Z') dst[i] = (char)(dst[i] + 32);
    }
    return out;
}

static int is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Return a copy of s with leading and trailing whitespace removed. */
DuxStr* duxrt_str_trim(DuxStr* s) {
    if (!s || s->len == 0) return duxrt_str_new("", 0);
    const char* p = sdata(s);
    int64_t start = 0, end = (int64_t)s->len;
    while (start < end && is_ws(p[start])) start++;
    while (end > start && is_ws(p[end - 1])) end--;
    return duxrt_str_new(p + start, end - start);
}

/* Return a copy of s with leading whitespace removed. */
DuxStr* duxrt_str_trim_start(DuxStr* s) {
    if (!s || s->len == 0) return duxrt_str_new("", 0);
    const char* p = sdata(s);
    int64_t i = 0;
    while (i < (int64_t)s->len && is_ws(p[i])) i++;
    return duxrt_str_new(p + i, (int64_t)s->len - i);
}

/* Return a copy of s with trailing whitespace removed. */
DuxStr* duxrt_str_trim_end(DuxStr* s) {
    if (!s || s->len == 0) return duxrt_str_new("", 0);
    const char* p = sdata(s);
    int64_t end = (int64_t)s->len;
    while (end > 0 && is_ws(p[end - 1])) end--;
    return duxrt_str_new(p, end);
}

/* Return s repeated n times.  Returns "" when n <= 0 or s is empty. */
DuxStr* duxrt_str_repeat(DuxStr* s, int64_t n) {
    if (!s || s->len == 0 || n <= 0) return duxrt_str_new("", 0);
    int64_t total = (int64_t)s->len * n;
    DuxStr* out = duxrt_str_new(NULL, total);
    char* dst = out->ext ? out->ext : out->data;
    const char* src = sdata(s);
    for (int64_t i = 0; i < n; i++) {
        memcpy(dst + i * (int64_t)s->len, src, (size_t)s->len);
    }
    dst[total] = '\0';
    return out;
}

/* Split s on literal separator sep.  Returns a DuxList* of DuxStr* values.
   When sep is "" or NULL, splits into individual characters.
   The list and all elements are newly allocated (caller owns them). */
DuxList* duxrt_str_split(DuxStr* s, DuxStr* sep) {
    DuxList* result = duxrt_list_new();
    if (!s) return result;
    const char* sp = sdata(s);
    if (!sep || sep->len == 0) {
        for (int64_t i = 0; i < (int64_t)s->len; i++) {
            duxrt_list_push(result, duxrt_str_new(sp + i, 1));
        }
        return result;
    }
    const char* fp = sdata(sep);
    int64_t prev = 0;
    for (int64_t i = 0; i <= (int64_t)s->len - (int64_t)sep->len; ) {
        if (memcmp(sp + i, fp, (size_t)sep->len) == 0) {
            duxrt_list_push(result, duxrt_str_new(sp + prev, i - prev));
            i += (int64_t)sep->len;
            prev = i;
        } else {
            i++;
        }
    }
    duxrt_list_push(result, duxrt_str_new(sp + prev, (int64_t)s->len - prev));
    return result;
}

/* Return 1 if s is a valid base-10 integer literal, else 0. */
int duxrt_str_to_long_valid(DuxStr* s) {
    if (!s || s->len == 0) return 0;
    const char* p = sdata(s);
    char* end;
    strtol(p, &end, 10);
    return (end != p && *end == '\0') ? 1 : 0;
}

/* Parse s as a base-10 integer.  Returns 0 if invalid (check with
   duxrt_str_to_long_valid first). */
int64_t duxrt_str_to_long_val(DuxStr* s) {
    if (!s || s->len == 0) return 0;
    return (int64_t)strtol(sdata(s), NULL, 10);
}

/* Return 1 if s is a valid floating-point literal, else 0. */
int duxrt_str_to_double_valid(DuxStr* s) {
    if (!s || s->len == 0) return 0;
    const char* p = sdata(s);
    char* end;
    strtod(p, &end);
    return (end != p && *end == '\0') ? 1 : 0;
}

/* Parse s as a double.  Returns 0.0 if invalid. */
double duxrt_str_to_double_val(DuxStr* s) {
    if (!s || s->len == 0) return 0.0;
    return strtod(sdata(s), NULL);
}
