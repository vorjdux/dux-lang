/*
 * duxrt — byte buffer runtime
 *
 * Provides a mutable byte buffer for the data.bytes stdlib module.
 * DuxBytes is a dynamic array of uint8_t.
 */
#include "duxrt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Constructor / destructor ────────────────────────────────────────────── */

DuxBytes* duxrt_bytes_create(void) {
    DuxBytes* b = (DuxBytes*)duxrt_alloc(sizeof(DuxBytes));
    b->len  = 0;
    b->cap  = 0;
    b->data = NULL;
    return b;
}

/* Alias kept for internal use */
DuxBytes* duxrt_bytes_new(void) { return duxrt_bytes_create(); }

DuxBytes* duxrt_bytes_with_capacity(int64_t cap) {
    DuxBytes* b = duxrt_bytes_create();
    if (cap > 0) {
        b->data = (uint8_t*)duxrt_alloc(cap);
        b->cap  = cap;
    }
    return b;
}

void duxrt_bytes_free(DuxBytes* b) {
    if (!b) return;
    if (b->data) duxrt_free(b->data);
    duxrt_free(b);
}

/* ── Capacity management ─────────────────────────────────────────────────── */

static void bytes_grow(DuxBytes* b, int64_t needed) {
    if (needed <= b->cap) return;
    int64_t new_cap = b->cap == 0 ? 16 : b->cap * 2;
    while (new_cap < needed) new_cap *= 2;
    uint8_t* new_data = (uint8_t*)realloc(b->data, (size_t)new_cap);
    if (!new_data) return;
    b->data = new_data;
    b->cap  = new_cap;
}

/* ── Append ──────────────────────────────────────────────────────────────── */

void duxrt_bytes_push(DuxBytes* b, int64_t byte_val) {
    if (!b) return;
    bytes_grow(b, b->len + 1);
    b->data[b->len++] = (uint8_t)(byte_val & 0xFF);
}

void duxrt_bytes_push_str(DuxBytes* b, DuxStr* s) {
    if (!b || !s) return;
    int64_t len = s->len;
    bytes_grow(b, b->len + len);
    const char* src = duxrt_str_cstr(s);
    memcpy(b->data + b->len, src, (size_t)len);
    b->len += len;
}

/* ── Access ──────────────────────────────────────────────────────────────── */

int64_t duxrt_bytes_at(DuxBytes* b, int64_t idx) {
    if (!b || idx < 0 || idx >= b->len) return -1;
    return (int64_t)b->data[idx];
}

void duxrt_bytes_poke(DuxBytes* b, int64_t idx, int64_t val) {
    if (!b || idx < 0 || idx >= b->len) return;
    b->data[idx] = (uint8_t)(val & 0xFF);
}

int64_t duxrt_bytes_len(DuxBytes* b) {
    if (!b) return 0;
    return b->len;
}

/* ── Conversion ──────────────────────────────────────────────────────────── */

DuxStr* duxrt_bytes_to_str(DuxBytes* b) {
    if (!b || b->len == 0) return duxrt_str_new("", 0);
    return duxrt_str_new((const char*)b->data, b->len);
}

DuxBytes* duxrt_bytes_from_str(DuxStr* s) {
    if (!s) return duxrt_bytes_new();
    DuxBytes* b = duxrt_bytes_with_capacity(s->len);
    const char* src = duxrt_str_cstr(s);
    memcpy(b->data, src, (size_t)s->len);
    b->len = s->len;
    return b;
}

/* ── Slice / copy ────────────────────────────────────────────────────────── */

DuxBytes* duxrt_bytes_slice(DuxBytes* b, int64_t start, int64_t end) {
    if (!b) return duxrt_bytes_create();
    if (start < 0) start = 0;
    if (end > b->len) end = b->len;
    if (start >= end) return duxrt_bytes_create();
    int64_t len = end - start;
    DuxBytes* result = duxrt_bytes_with_capacity(len);
    memcpy(result->data, b->data + start, (size_t)len);
    result->len = len;
    return result;
}

/* ── Fill / clear ────────────────────────────────────────────────────────── */

void duxrt_bytes_clear(DuxBytes* b) {
    if (b) b->len = 0;
}

void duxrt_bytes_fill(DuxBytes* b, int64_t val, int64_t count) {
    if (!b || count <= 0) return;
    bytes_grow(b, b->len + count);
    memset(b->data + b->len, (int)(val & 0xFF), (size_t)count);
    b->len += count;
}
