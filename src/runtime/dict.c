#include "duxrt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define DICT_INIT_CAP  16
#define DICT_LOAD_NUM   3
#define DICT_LOAD_DEN   4   /* resize when len > cap * 3/4 */

/* Tombstone sentinel: a deleted slot that must not break probe chains. */
#define TOMBSTONE ((char*)(uintptr_t)1)

static inline int is_empty(char* k)     { return k == NULL; }
static inline int is_tombstone(char* k) { return k == TOMBSTONE; }
static inline int is_live(char* k)      { return k != NULL && k != TOMBSTONE; }

static uint64_t hash_str(const char* s) {
    /* FNV-1a 64-bit */
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

DuxDict* duxrt_dict_new(void) {
    DuxDict* d = (DuxDict*)malloc(sizeof(DuxDict));
    if (!d) abort();
    d->len  = 0;
    d->cap  = DICT_INIT_CAP;
    d->tomb = 0;
    d->entries = (DuxDictEntry*)calloc((size_t)DICT_INIT_CAP, sizeof(DuxDictEntry));
    if (!d->entries) abort();
    return d;
}

static void dict_insert_raw(DuxDict* d, char* key, void* val) {
    uint64_t h   = hash_str(key);
    int64_t  idx = (int64_t)(h & (uint64_t)(d->cap - 1));
    int64_t  tombstone_idx = -1;   /* first tombstone seen, candidate for insertion */
    while (!is_empty(d->entries[idx].key)) {
        if (is_tombstone(d->entries[idx].key)) {
            /* Remember first tombstone; keep probing for existing key */
            if (tombstone_idx < 0) tombstone_idx = idx;
        } else if (strcmp(d->entries[idx].key, key) == 0) {
            /* Update path: free the incoming key (caller strdup'd it) */
            free(key);
            d->entries[idx].val = val;
            return;
        }
        idx = (idx + 1) & (d->cap - 1);
    }
    /* Key not found: insert at tombstone slot if available, else at empty slot */
    if (tombstone_idx >= 0) idx = tombstone_idx;
    d->entries[idx].key = key;
    d->entries[idx].val = val;
    d->len++;
}

static void dict_grow(DuxDict* d) {
    int64_t       old_cap  = d->cap;
    DuxDictEntry* old_ents = d->entries;
    d->cap    *= 2;
    d->len     = 0;
    d->tomb    = 0;   /* tombstones are cleared on resize */
    d->entries = (DuxDictEntry*)calloc((size_t)d->cap, sizeof(DuxDictEntry));
    if (!d->entries) abort();
    for (int64_t i = 0; i < old_cap; ++i) {
        /* Skip empty slots and tombstones — only reinsert live entries */
        if (is_live(old_ents[i].key))
            dict_insert_raw(d, old_ents[i].key, old_ents[i].val);
    }
    free(old_ents);
}

void duxrt_dict_set(DuxDict* d, const char* key, void* val) {
    if (!d || !key) return;
    if ((d->len + d->tomb) * DICT_LOAD_DEN >= d->cap * DICT_LOAD_NUM)
        dict_grow(d);
    char* k = strdup(key);
    if (!k) abort();
    dict_insert_raw(d, k, val);
}

void* duxrt_dict_get(DuxDict* d, const char* key) {
    if (!d || !key) return NULL;
    uint64_t h   = hash_str(key);
    int64_t  idx = (int64_t)(h & (uint64_t)(d->cap - 1));
    while (!is_empty(d->entries[idx].key)) {
        /* Probe past tombstones; only match live entries */
        if (is_live(d->entries[idx].key) &&
            strcmp(d->entries[idx].key, key) == 0)
            return d->entries[idx].val;
        idx = (idx + 1) & (d->cap - 1);
    }
    return NULL;
}

int duxrt_dict_has(DuxDict* d, const char* key) {
    if (!d || !key) return 0;
    uint64_t h   = hash_str(key);
    int64_t  idx = (int64_t)(h & (uint64_t)(d->cap - 1));
    while (!is_empty(d->entries[idx].key)) {
        if (is_live(d->entries[idx].key) &&
            strcmp(d->entries[idx].key, key) == 0) return 1;
        idx = (idx + 1) & (d->cap - 1);
    }
    return 0;
}

void duxrt_dict_del(DuxDict* d, const char* key) {
    if (!d || !key) return;
    uint64_t h   = hash_str(key);
    int64_t  idx = (int64_t)(h & (uint64_t)(d->cap - 1));
    while (!is_empty(d->entries[idx].key)) {
        if (is_live(d->entries[idx].key) &&
            strcmp(d->entries[idx].key, key) == 0) {
            free(d->entries[idx].key);
            d->entries[idx].key = TOMBSTONE;  /* mark deleted; preserve probe chain */
            d->entries[idx].val = NULL;
            d->len--;
            d->tomb++;
            return;
        }
        idx = (idx + 1) & (d->cap - 1);
    }
}

void duxrt_dict_free(DuxDict* d) {
    if (!d) return;
    for (int64_t i = 0; i < d->cap; ++i)
        if (is_live(d->entries[i].key)) free(d->entries[i].key);
    free(d->entries);
    free(d);
}
