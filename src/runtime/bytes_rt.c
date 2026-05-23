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
