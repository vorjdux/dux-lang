#include "duxrt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LIST_INIT_CAP 8

DuxList* duxrt_list_new(void) {
    DuxList* l = (DuxList*)malloc(sizeof(DuxList));
    if (!l) abort();
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
    if (idx < 0) idx += l->len;
    if (idx < 0 || idx >= l->len) {
        fprintf(stderr, "duxrt: list index %ld out of range (len=%ld)\n",
                (long)idx, (long)l->len);
        abort();
    }
    return l->data[idx];
}

void duxrt_list_set(DuxList* l, int64_t idx, void* val) {
    if (!l) return;
    if (idx < 0) idx += l->len;
    if (idx < 0 || idx >= l->len) {
        fprintf(stderr, "duxrt: list index %ld out of range\n", (long)idx);
        abort();
    }
    l->data[idx] = val;
}

int64_t duxrt_list_len(DuxList* l) {
    return l ? l->len : 0;
}

void duxrt_list_free(DuxList* l) {
    if (!l) return;
    free(l->data);
    free(l);
}
