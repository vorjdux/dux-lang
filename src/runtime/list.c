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

/* ── Generic (void*-element) list ───────────────────────────────────────── */

DuxList* duxrt_list_new(void) {
    DuxList* l = (DuxList*)malloc(sizeof(DuxList));
    if (!l) abort();
    atomic_store_explicit(&l->refcount, 1, memory_order_relaxed);
    l->elem_kind = DUXLIST_ELEM_PTR;
    l->_pad[0] = l->_pad[1] = l->_pad[2] = 0;
    l->len  = 0;
    l->cap  = LIST_INIT_CAP;
    l->data = malloc(sizeof(void*) * (size_t)LIST_INIT_CAP);
    if (!l->data) abort();
    return l;
}

void duxrt_list_push(DuxList* l, void* val) {
    if (l->len == l->cap) {
        l->cap *= 2;
        l->data = realloc(l->data, sizeof(void*) * (size_t)l->cap);
        if (!l->data) abort();
    }
    ((void**)l->data)[l->len++] = val;
}

void* duxrt_list_get(DuxList* l, int64_t idx) {
    if (!l) return NULL;
    int64_t orig = idx;
    if (idx < 0) idx += l->len;
    if (idx < 0 || idx >= l->len) {
        list_index_error(orig, l->len);
        return NULL;  /* unreachable — list_index_error throws */
    }
    /* For typed i32 lists, pack the int32_t value into the void* bits so that
       callers using the generic box_to_ptr/unbox_from_ptr convention work correctly.
       The hot path (gen_for_in with a statically-typed list) bypasses this via
       direct GEP in the codegen and never calls duxrt_list_get at all. */
    if (l->elem_kind == DUXLIST_ELEM_I32) {
        int32_t v = ((int32_t*)l->data)[idx];
        return (void*)(uintptr_t)(uint32_t)v;
    }
    return ((void**)l->data)[idx];
}

void duxrt_list_set(DuxList* l, int64_t idx, void* val) {
    if (!l) return;
    int64_t orig = idx;
    if (idx < 0) idx += l->len;
    if (idx < 0 || idx >= l->len) {
        list_index_error(orig, l->len);
        return;
    }
    /* For typed i32 lists, unpack the void* box convention back to int32_t. */
    if (l->elem_kind == DUXLIST_ELEM_I32) {
        ((int32_t*)l->data)[idx] = (int32_t)(uintptr_t)val;
        return;
    }
    ((void**)l->data)[idx] = val;
}

int64_t duxrt_list_len(DuxList* l) {
    return l ? l->len : 0;
}

/* ── Typed int32_t-element list ─────────────────────────────────────────── */

DuxList* duxrt_list_new_i32(void) {
    DuxList* l = (DuxList*)malloc(sizeof(DuxList));
    if (!l) abort();
    atomic_store_explicit(&l->refcount, 1, memory_order_relaxed);
    l->elem_kind = DUXLIST_ELEM_I32;
    l->_pad[0] = l->_pad[1] = l->_pad[2] = 0;
    l->len  = 0;
    l->cap  = LIST_INIT_CAP;
    l->data = malloc(sizeof(int32_t) * (size_t)LIST_INIT_CAP);
    if (!l->data) abort();
    return l;
}

void duxrt_list_push_i32(DuxList* l, int32_t val) {
    if (l->len == l->cap) {
        l->cap *= 2;
        l->data = realloc(l->data, sizeof(int32_t) * (size_t)l->cap);
        if (!l->data) abort();
    }
    ((int32_t*)l->data)[l->len++] = val;
}

int32_t duxrt_list_get_i32(DuxList* l, int64_t idx) {
    if (!l) return 0;
    int64_t orig = idx;
    if (idx < 0) idx += l->len;
    if (idx < 0 || idx >= l->len) {
        list_index_error(orig, l->len);
        return 0;
    }
    return ((int32_t*)l->data)[idx];
}

void duxrt_list_set_i32(DuxList* l, int64_t idx, int32_t val) {
    if (!l) return;
    int64_t orig = idx;
    if (idx < 0) idx += l->len;
    if (idx < 0 || idx >= l->len) {
        list_index_error(orig, l->len);
        return;
    }
    ((int32_t*)l->data)[idx] = val;
}

/* ── Reference counting ─────────────────────────────────────────────────── */

DuxList* duxrt_list_retain(DuxList* l) {
    if (!l) return NULL;
    atomic_fetch_add_explicit(&l->refcount, 1, memory_order_relaxed);
    return l;
}

void duxrt_list_release(DuxList* l) {
    if (!l) return;
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
            duxrt_list_push(out, duxrt_list_get(a, i));
    }
    if (b) {
        for (int64_t i = 0; i < b->len; i++)
            duxrt_list_push(out, duxrt_list_get(b, i));
    }
    return out;
}
