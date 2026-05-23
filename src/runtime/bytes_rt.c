/*
 * bytes_rt.c — byte buffer runtime for Dux stdlib data.bytes
 */
#include "duxrt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* DuxBytes: a simple heap byte buffer (opaque to callers via void*) */
typedef struct {
    uint8_t* data;
    int64_t  len;
    int64_t  cap;
} DuxBytes;

/* Allocate a new DuxBytes buffer */
void* duxrt_bytes_new(int64_t cap) {
    DuxBytes* b = (DuxBytes*)malloc(sizeof(DuxBytes));
    if (!b) abort();
    if (cap < 8) cap = 8;
    b->data = (uint8_t*)malloc((size_t)cap);
    if (!b->data) abort();
    b->len = 0;
    b->cap = cap;
    return b;
}

/* Create a DuxBytes from a DuxStr */
void* duxrt_bytes_from_str(DuxStr* s) {
    if (!s) return duxrt_bytes_new(8);
    int64_t n = (int64_t)s->len;
    DuxBytes* b = (DuxBytes*)duxrt_bytes_new(n + 1);
    memcpy(b->data, duxrt_str_cstr(s), (size_t)n);
    b->len = n;
    return b;
}

/* Convert DuxBytes to DuxStr */
DuxStr* duxrt_bytes_to_str(void* handle) {
    DuxBytes* b = (DuxBytes*)handle;
    if (!b || b->len == 0) return duxrt_str_new("", 0);
    return duxrt_str_new((const char*)b->data, b->len);
}

int64_t duxrt_bytes_len(void* handle) {
    DuxBytes* b = (DuxBytes*)handle;
    return b ? b->len : 0;
}

int64_t duxrt_bytes_get(void* handle, int64_t i) {
    DuxBytes* b = (DuxBytes*)handle;
    if (!b || i < 0 || i >= b->len) return 0;
    return (int64_t)b->data[i];
}

void duxrt_bytes_set(void* handle, int64_t i, int64_t val) {
    DuxBytes* b = (DuxBytes*)handle;
    if (!b || i < 0 || i >= b->len) return;
    b->data[i] = (uint8_t)(val & 0xFF);
}

void duxrt_bytes_push(void* handle, int64_t val) {
    DuxBytes* b = (DuxBytes*)handle;
    if (!b) return;
    if (b->len >= b->cap) {
        b->cap *= 2;
        b->data = (uint8_t*)realloc(b->data, (size_t)b->cap);
        if (!b->data) abort();
    }
    b->data[b->len++] = (uint8_t)(val & 0xFF);
}

void duxrt_bytes_append(void* dst_handle, void* src_handle) {
    DuxBytes* dst = (DuxBytes*)dst_handle;
    DuxBytes* src = (DuxBytes*)src_handle;
    if (!dst || !src || src->len == 0) return;
    int64_t needed = dst->len + src->len;
    if (needed > dst->cap) {
        while (dst->cap < needed) dst->cap *= 2;
        dst->data = (uint8_t*)realloc(dst->data, (size_t)dst->cap);
        if (!dst->data) abort();
    }
    memcpy(dst->data + dst->len, src->data, (size_t)src->len);
    dst->len += src->len;
}

void duxrt_bytes_free(void* handle) {
    DuxBytes* b = (DuxBytes*)handle;
    if (!b) return;
    free(b->data);
    free(b);
}

/* ── Typed endian-aware read/write ──────────────────────────────────────── */

static void bytes_ensure_cap(DuxBytes* b, int64_t needed) {
    while (b->cap < b->len + needed) b->cap *= 2;
    b->data = (uint8_t*)realloc(b->data, (size_t)b->cap);
    if (!b->data) abort();
}

void duxrt_bytes_write_u8(void* h, int32_t v) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b) return;
    bytes_ensure_cap(b, 1);
    b->data[b->len++] = (uint8_t)(v & 0xFF);
}

void duxrt_bytes_write_u16_le(void* h, int32_t v) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b) return;
    bytes_ensure_cap(b, 2);
    b->data[b->len++] = (uint8_t)(v & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFF);
}

void duxrt_bytes_write_u16_be(void* h, int32_t v) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b) return;
    bytes_ensure_cap(b, 2);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFF);
    b->data[b->len++] = (uint8_t)(v & 0xFF);
}

void duxrt_bytes_write_u32_le(void* h, int64_t v) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b) return;
    bytes_ensure_cap(b, 4);
    b->data[b->len++] = (uint8_t)(v & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 16) & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 24) & 0xFF);
}

void duxrt_bytes_write_u32_be(void* h, int64_t v) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b) return;
    bytes_ensure_cap(b, 4);
    b->data[b->len++] = (uint8_t)((v >> 24) & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 16) & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFF);
    b->data[b->len++] = (uint8_t)(v & 0xFF);
}

void duxrt_bytes_write_u64_le(void* h, int64_t v) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b) return;
    bytes_ensure_cap(b, 8);
    for (int i = 0; i < 8; i++)
        b->data[b->len++] = (uint8_t)((v >> (i * 8)) & 0xFF);
}

void duxrt_bytes_write_u64_be(void* h, int64_t v) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b) return;
    bytes_ensure_cap(b, 8);
    for (int i = 7; i >= 0; i--)
        b->data[b->len++] = (uint8_t)((v >> (i * 8)) & 0xFF);
}

int32_t duxrt_bytes_read_u8(void* h, int64_t off) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b || off < 0 || off >= b->len) return 0;
    return (int32_t)b->data[off];
}

int32_t duxrt_bytes_read_u16_le(void* h, int64_t off) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b || off < 0 || off + 2 > b->len) return 0;
    return (int32_t)b->data[off] | ((int32_t)b->data[off+1] << 8);
}

int32_t duxrt_bytes_read_u16_be(void* h, int64_t off) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b || off < 0 || off + 2 > b->len) return 0;
    return ((int32_t)b->data[off] << 8) | (int32_t)b->data[off+1];
}

int64_t duxrt_bytes_read_u32_le(void* h, int64_t off) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b || off < 0 || off + 4 > b->len) return 0;
    return (int64_t)b->data[off] | ((int64_t)b->data[off+1] << 8) |
           ((int64_t)b->data[off+2] << 16) | ((int64_t)b->data[off+3] << 24);
}

int64_t duxrt_bytes_read_u32_be(void* h, int64_t off) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b || off < 0 || off + 4 > b->len) return 0;
    return ((int64_t)b->data[off] << 24) | ((int64_t)b->data[off+1] << 16) |
           ((int64_t)b->data[off+2] << 8) | (int64_t)b->data[off+3];
}

int64_t duxrt_bytes_read_u64_le(void* h, int64_t off) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b || off < 0 || off + 8 > b->len) return 0;
    int64_t r = 0;
    for (int i = 0; i < 8; i++)
        r |= ((int64_t)b->data[off + i]) << (i * 8);
    return r;
}

int64_t duxrt_bytes_read_u64_be(void* h, int64_t off) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b || off < 0 || off + 8 > b->len) return 0;
    int64_t r = 0;
    for (int i = 0; i < 8; i++)
        r = (r << 8) | (int64_t)b->data[off + i];
    return r;
}

DuxStr* duxrt_bytes_hex(void* h) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b || b->len == 0) return duxrt_str_new("", 0);
    char* buf = (char*)malloc((size_t)(b->len * 2 + 1));
    if (!buf) return duxrt_str_new("", 0);
    for (int64_t i = 0; i < b->len; i++)
        snprintf(buf + i * 2, 3, "%02x", (unsigned)b->data[i]);
    DuxStr* s = duxrt_str_new(buf, b->len * 2);
    free(buf);
    return s;
}

void* duxrt_bytes_slice(void* h, int64_t start, int64_t end_pos) {
    DuxBytes* b = (DuxBytes*)h;
    if (!b) return duxrt_bytes_new(8);
    if (start < 0) start = 0;
    if (end_pos > b->len) end_pos = b->len;
    if (start >= end_pos) return duxrt_bytes_new(8);
    int64_t n = end_pos - start;
    DuxBytes* out = (DuxBytes*)duxrt_bytes_new(n);
    memcpy(out->data, b->data + start, (size_t)n);
    out->len = n;
    return out;
}
