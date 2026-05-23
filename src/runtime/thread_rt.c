/*
 * duxrt — thread runtime
 *
 * Provides POSIX thread creation and synchronisation for the thread
 * stdlib module.  The Dux thread API is intentionally simple: spawn a
 * function pointer (void→void) in a new OS thread, then join on a handle.
 *
 * Mutex and condition variable primitives are also exposed so that
 * user code can build higher-level concurrency patterns.
 */
#define _GNU_SOURCE
#include "duxrt.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Thread handle ───────────────────────────────────────────────────────── */

typedef struct DuxThread {
    pthread_t      tid;
    int            joined;
} DuxThread;

/* Trampoline: the function the runtime calls to start a thread */
typedef struct {
    void (*fn)(void*);
    void* arg;
} ThreadEntry;

static void* thread_trampoline(void* raw) {
    ThreadEntry* e = (ThreadEntry*)raw;
    e->fn(e->arg);
    free(e);
    return NULL;
}

/* ── Spawn ───────────────────────────────────────────────────────────────── */

DuxThread* duxrt_thread_spawn(void (*fn)(void*), void* arg) {
    DuxThread* t = (DuxThread*)malloc(sizeof(DuxThread));
    if (!t) return NULL;
    t->joined = 0;
    ThreadEntry* entry = (ThreadEntry*)malloc(sizeof(ThreadEntry));
    if (!entry) { free(t); return NULL; }
    entry->fn  = fn;
    entry->arg = arg;
    if (pthread_create(&t->tid, NULL, thread_trampoline, entry) != 0) {
        free(entry);
        free(t);
        return NULL;
    }
    return t;
}

/* ── Join ────────────────────────────────────────────────────────────────── */

void duxrt_thread_join(DuxThread* t) {
    if (!t || t->joined) return;
    pthread_join(t->tid, NULL);
    t->joined = 1;
    free(t);
}

/* ── Detach ──────────────────────────────────────────────────────────────── */

void duxrt_thread_detach(DuxThread* t) {
    if (!t || t->joined) return;
    pthread_detach(t->tid);
    t->joined = 1;
    free(t);
}

/* ── Sleep ───────────────────────────────────────────────────────────────── */

void duxrt_thread_sleep_ms(int64_t ms) {
    struct timespec req;
    req.tv_sec  = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000LL;
    nanosleep(&req, NULL);
}

/* ── Mutex ───────────────────────────────────────────────────────────────── */

typedef struct DuxMutex {
    pthread_mutex_t m;
} DuxMutex;

DuxMutex* duxrt_mutex_create(void) {
    DuxMutex* mx = (DuxMutex*)malloc(sizeof(DuxMutex));
    if (!mx) return NULL;
    pthread_mutex_init(&mx->m, NULL);
    return mx;
}

void duxrt_mutex_lock(DuxMutex* mx) {
    if (mx) pthread_mutex_lock(&mx->m);
}

void duxrt_mutex_unlock(DuxMutex* mx) {
    if (mx) pthread_mutex_unlock(&mx->m);
}

void duxrt_mutex_free(DuxMutex* mx) {
    if (!mx) return;
    pthread_mutex_destroy(&mx->m);
    free(mx);
}

/* ── Thread ID ───────────────────────────────────────────────────────────── */

int64_t duxrt_thread_id(void) {
    return (int64_t)(uintptr_t)pthread_self();
}
