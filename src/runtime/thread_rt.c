/*
 * thread_rt.c — threading runtime for Dux stdlib thread.thread / thread.mutex
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
