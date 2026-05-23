/*
 * future_rt.c — DuxFuture: thread-backed async/await primitive
 *
 * A DuxFuture represents a value that will be produced by a detached thread.
 * The caller can block on duxrt_future_await() to retrieve the result.
 *
 * Lifecycle:
 *   1. Launcher creates a future with duxrt_future_new().
 *   2. Launcher spawns a detached thread (via duxrt_future_spawn_detached).
 *   3. Worker thread calls duxrt_future_set() when done.
 *   4. Awaiter calls duxrt_future_await() — blocks until step 3.
 *   5. Awaiter frees the future with duxrt_future_free().
 */
#include "duxrt.h"
#include <pthread.h>
#include <stdlib.h>

typedef struct DuxFuture {
    pthread_mutex_t mu;
    pthread_cond_t  cond;
    void*           result;
    int             done;
} DuxFuture;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

void* duxrt_future_new(void) {
    DuxFuture* f = (DuxFuture*)calloc(1, sizeof(DuxFuture));
    if (!f) return NULL;
    pthread_mutex_init(&f->mu, NULL);
    pthread_cond_init(&f->cond, NULL);
    return f;
}

void duxrt_future_free(void* fut) {
    DuxFuture* f = (DuxFuture*)fut;
    if (!f) return;
    pthread_mutex_destroy(&f->mu);
    pthread_cond_destroy(&f->cond);
    free(f);
}

/* ── Signal completion ───────────────────────────────────────────────────── */

/* Called by the async worker thread when it has computed its result. */
void duxrt_future_set(void* fut, void* result) {
    DuxFuture* f = (DuxFuture*)fut;
    if (!f) return;
    pthread_mutex_lock(&f->mu);
    f->result = result;
    f->done   = 1;
    pthread_cond_signal(&f->cond);
    pthread_mutex_unlock(&f->mu);
}

/* ── Await ───────────────────────────────────────────────────────────────── */

/* Block until the future is resolved; returns the result pointer. */
void* duxrt_future_await(void* fut) {
    DuxFuture* f = (DuxFuture*)fut;
    if (!f) return NULL;
    pthread_mutex_lock(&f->mu);
    while (!f->done)
        pthread_cond_wait(&f->cond, &f->mu);
    void* r = f->result;
    pthread_mutex_unlock(&f->mu);
    return r;
}

/* ── Spawn ───────────────────────────────────────────────────────────────── */

/*
 * Spawn a detached thread that calls fn(env).
 * fn must be a pthread-compatible worker: (void*) -> void*
 * The thread is detached so no join is needed; completion is signalled via
 * the future (duxrt_future_set).
 */
void duxrt_future_spawn_detached(void* fn, void* env) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t tid;
    pthread_create(&tid, &attr, (void*(*)(void*))fn, env);
    pthread_attr_destroy(&attr);
}
