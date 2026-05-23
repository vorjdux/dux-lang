/*
 * thread_rt.c — threading runtime for Dux stdlib thread module
 * Covers: Thread, Mutex, RWLock, CondVar, Once
 */
#include "duxrt.h"
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

void duxrt_thread_sleep_ms(int64_t ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int64_t duxrt_thread_id(void) {
    return (int64_t)(uintptr_t)pthread_self();
}

/* ── Thread spawn/join ───────────────────────────────────────────────────── */

typedef struct {
    void (*fn)(void*);
    void* arg;
} ThreadArgs;

static void* thread_runner(void* arg) {
    ThreadArgs* ta = (ThreadArgs*)arg;
    ta->fn(ta->arg);
    free(ta);
    return NULL;
}

void* duxrt_thread_spawn(void* fn, void* arg) {
    pthread_t* t = (pthread_t*)malloc(sizeof(pthread_t));
    if (!t) return NULL;
    ThreadArgs* ta = (ThreadArgs*)malloc(sizeof(ThreadArgs));
    if (!ta) { free(t); return NULL; }
    ta->fn  = (void (*)(void*))fn;
    ta->arg = arg;
    if (pthread_create(t, NULL, thread_runner, ta) != 0) {
        free(ta); free(t); return NULL;
    }
    return t;
}

int32_t duxrt_thread_join(void* handle) {
    if (!handle) return -1;
    pthread_t* t = (pthread_t*)handle;
    int r = pthread_join(*t, NULL);
    free(t);
    return r == 0 ? 0 : -1;
}

int32_t duxrt_thread_detach(void* handle) {
    if (!handle) return -1;
    pthread_t* t = (pthread_t*)handle;
    int r = pthread_detach(*t);
    free(t);
    return r == 0 ? 0 : -1;
}

/* ── Mutex ───────────────────────────────────────────────────────────────── */

void* duxrt_mutex_create(void) {
    pthread_mutex_t* m = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    if (!m) return NULL;
    pthread_mutex_init(m, NULL);
    return m;
}

void duxrt_mutex_lock(void* m) {
    if (m) pthread_mutex_lock((pthread_mutex_t*)m);
}

void duxrt_mutex_unlock(void* m) {
    if (m) pthread_mutex_unlock((pthread_mutex_t*)m);
}

int32_t duxrt_mutex_trylock(void* m) {
    if (!m) return -1;
    return pthread_mutex_trylock((pthread_mutex_t*)m) == 0 ? 0 : -1;
}

void duxrt_mutex_free(void* m) {
    if (!m) return;
    pthread_mutex_destroy((pthread_mutex_t*)m);
    free(m);
}

/* Alias: issue #93 uses duxrt_mutex_new */
void* duxrt_mutex_new(void) { return duxrt_mutex_create(); }

/* ── Thread self ─────────────────────────────────────────────────────────── */

void* duxrt_thread_self(void) {
    pthread_t* t = (pthread_t*)malloc(sizeof(pthread_t));
    if (!t) return NULL;
    *t = pthread_self();
    return t;
}

/* ── RWLock ──────────────────────────────────────────────────────────────── */

void* duxrt_rwlock_new(void) {
    pthread_rwlock_t* rw = (pthread_rwlock_t*)malloc(sizeof(pthread_rwlock_t));
    if (!rw) return NULL;
    pthread_rwlock_init(rw, NULL);
    return rw;
}

void duxrt_rwlock_rlock(void* rw) {
    if (rw) pthread_rwlock_rdlock((pthread_rwlock_t*)rw);
}

void duxrt_rwlock_wlock(void* rw) {
    if (rw) pthread_rwlock_wrlock((pthread_rwlock_t*)rw);
}

void duxrt_rwlock_unlock(void* rw) {
    if (rw) pthread_rwlock_unlock((pthread_rwlock_t*)rw);
}

void duxrt_rwlock_free(void* rw) {
    if (!rw) return;
    pthread_rwlock_destroy((pthread_rwlock_t*)rw);
    free(rw);
}

/* ── CondVar ─────────────────────────────────────────────────────────────── */

void* duxrt_cond_new(void) {
    pthread_cond_t* c = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
    if (!c) return NULL;
    pthread_cond_init(c, NULL);
    return c;
}

void duxrt_cond_wait(void* c, void* m) {
    if (c && m)
        pthread_cond_wait((pthread_cond_t*)c, (pthread_mutex_t*)m);
}

void duxrt_cond_signal(void* c) {
    if (c) pthread_cond_signal((pthread_cond_t*)c);
}

void duxrt_cond_broadcast(void* c) {
    if (c) pthread_cond_broadcast((pthread_cond_t*)c);
}

void duxrt_cond_free(void* c) {
    if (!c) return;
    pthread_cond_destroy((pthread_cond_t*)c);
    free(c);
}

/* ── Once ────────────────────────────────────────────────────────────────── */

typedef struct {
    pthread_once_t once;
    void (*fn)(void);
} DuxOnce;

/* pthread_once requires a static/global function — we store the fn ptr in TLS */
static __thread void (*once_fn_)(void) = NULL;
static void once_trampoline(void) { if (once_fn_) once_fn_(); }

void* duxrt_once_new(void) {
    DuxOnce* o = (DuxOnce*)malloc(sizeof(DuxOnce));
    if (!o) return NULL;
    pthread_once_t init = PTHREAD_ONCE_INIT;
    o->once = init;
    o->fn   = NULL;
    return o;
}

void duxrt_once_call(void* handle, void* fn) {
    DuxOnce* o = (DuxOnce*)handle;
    if (!o || !fn) return;
    once_fn_ = (void (*)(void))fn;
    pthread_once(&o->once, once_trampoline);
}

void duxrt_once_free(void* handle) {
    free(handle);
}
