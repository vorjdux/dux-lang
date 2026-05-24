#include "duxrt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

/* Throw a Dux IndexError; msg must be a string literal (or static storage). */
static void list_index_error(int64_t idx, int64_t len) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "list index %ld out of range (len=%ld)", (long)idx, (long)len);
    duxrt_throw(duxrt_exception_new("IndexError", buf));
}

#define LIST_INIT_CAP 8

DuxList* duxrt_list_new(void) {
    DuxList* l = (DuxList*)malloc(sizeof(DuxList));
    if (!l) abort();
    atomic_store_explicit(&l->refcount, 1, memory_order_relaxed);
    l->_pad = 0;
    l->len  = 0;
    l->cap  = LIST_INIT_CAP;
    l->data = (void**)malloc(sizeof(void*) * (size_t)LIST_INIT_CAP);
    if (!l->data) abort();
    return l;
}

void duxrt_list_push(DuxList* l, void* val) {
    if (l->len == l->cap) {
        l->cap *= 2;
        l->data = (void**)realloc(l->data, sizeof(void*) * (size_t)l->cap);
        if (!l->data) abort();
    }
    l->data[l->len++] = val;
}

void* duxrt_list_get(DuxList* l, int64_t idx) {
    if (!l) return NULL;
    int64_t orig = idx;
    if (idx < 0) idx += l->len;
    if (idx < 0 || idx >= l->len) {
        list_index_error(orig, l->len);
        return NULL;  /* unreachable — list_index_error throws */
    }
    return l->data[idx];
}

void duxrt_list_set(DuxList* l, int64_t idx, void* val) {
    if (!l) return;
    int64_t orig = idx;
    if (idx < 0) idx += l->len;
    if (idx < 0 || idx >= l->len) {
        list_index_error(orig, l->len);
        return;  /* unreachable — list_index_error throws */
    }
    l->data[idx] = val;
}

int64_t duxrt_list_len(DuxList* l) {
    return l ? l->len : 0;
}

/* ── Reference counting ─────────────────────────────────────────────────── */

DuxList* duxrt_list_retain(DuxList* l) {
    if (!l) return NULL;
    atomic_fetch_add_explicit(&l->refcount, 1, memory_order_relaxed);
    return l;
}

void duxrt_list_release(DuxList* l) {
    if (!l) return;
    /* fetch_sub returns the value BEFORE subtraction; free when it was 1 */
    if (atomic_fetch_sub_explicit(&l->refcount, 1, memory_order_acq_rel) == 1) {
        free(l->data);
        free(l);
    }
}

/* Legacy name — release is the preferred API. */
void duxrt_list_free(DuxList* l) {
    duxrt_list_release(l);
}

/* ── Concatenation ──────────────────────────────────────────────────────── */

DuxList* duxrt_list_concat(DuxList* a, DuxList* b) {
    DuxList* out = duxrt_list_new();
    if (a) {
        for (int64_t i = 0; i < a->len; i++)
            duxrt_list_push(out, a->data[i]);
    }
    if (b) {
        for (int64_t i = 0; i < b->len; i++)
            duxrt_list_push(out, b->data[i]);
    }
    return out;
}
