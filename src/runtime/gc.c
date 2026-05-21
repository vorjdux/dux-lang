#include "duxrt.h"
#include <stdlib.h>
#include <string.h>

/*
 * Minimal bump-pointer arena for string allocations.
 * Eliminates exit-time leaks without per-string free overhead.
 * All registered strings are freed when duxrt_str_arena_free_all() is called,
 * which is registered with atexit() automatically.
 *
 * Limitation: does not reclaim memory during program execution.
 * A proper GC is needed for long-running programs with heavy string use.
 */

#define ARENA_BLOCK_SIZE (65536)  /* 64 KiB */

typedef struct StrArenaBlock {
    char*                 data;
    size_t                used;
    size_t                cap;
    struct StrArenaBlock* next;
} StrArenaBlock;

static StrArenaBlock* str_arena_head = NULL;

void* duxrt_str_arena_alloc(size_t size) {
    /* align to 8 bytes */
    size = (size + 7u) & ~(size_t)7u;
    if (!str_arena_head || str_arena_head->used + size > str_arena_head->cap) {
        size_t cap = size > ARENA_BLOCK_SIZE ? size : ARENA_BLOCK_SIZE;
        StrArenaBlock* b = (StrArenaBlock*)malloc(sizeof(StrArenaBlock));
        if (!b) abort();
        b->data = (char*)malloc(cap);
        if (!b->data) abort();
        b->used = 0;
        b->cap  = cap;
        b->next = str_arena_head;
        str_arena_head = b;
    }
    void* p = str_arena_head->data + str_arena_head->used;
    str_arena_head->used += size;
    return p;
}

void duxrt_str_arena_free_all(void) {
    StrArenaBlock* b = str_arena_head;
    while (b) {
        StrArenaBlock* next = b->next;
        free(b->data);
        free(b);
        b = next;
    }
    str_arena_head = NULL;
}

/* Registered with atexit so cleanup is automatic */
__attribute__((constructor))
static void str_arena_init(void) {
    atexit(duxrt_str_arena_free_all);
}
