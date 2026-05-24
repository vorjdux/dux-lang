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
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct DuxFuture {
    pthread_mutex_t  mu;
    pthread_cond_t   cond;
    void*            result;
    int              done;
    atomic_int       ref_count;  /* ref-counted: free when count reaches 0 */
} DuxFuture;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

void* duxrt_future_new(void) {
    DuxFuture* f = (DuxFuture*)calloc(1, sizeof(DuxFuture));
    if (!f) return NULL;
    pthread_mutex_init(&f->mu, NULL);
    pthread_cond_init(&f->cond, NULL);
    atomic_init(&f->ref_count, 1);
    return f;
}

/* Increment the reference count (borrow a new owning reference). */
void duxrt_future_retain(void* fut) {
    DuxFuture* f = (DuxFuture*)fut;
    if (!f) return;
    atomic_fetch_add_explicit(&f->ref_count, 1, memory_order_relaxed);
}

/* Release one owning reference.  Destroys the future when the count
 * reaches zero.  It is safe to call only after the future is done
 * (i.e. after duxrt_future_await returns), which guarantees no thread
 * holds the mutex — so pthread_mutex_destroy is well-defined (RT-6). */
void duxrt_future_free(void* fut) {
    DuxFuture* f = (DuxFuture*)fut;
    if (!f) return;
    if (atomic_fetch_sub_explicit(&f->ref_count, 1, memory_order_acq_rel) != 1)
        return;  /* still has other owners */
    /* Only destroy when the future is settled — avoids UB from destroying
     * a mutex that may still be held by the worker thread (RT-6). */
    pthread_mutex_lock(&f->mu);
    while (!f->done)
        pthread_cond_wait(&f->cond, &f->mu);
    pthread_mutex_unlock(&f->mu);
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

/* ── Global thread pool ──────────────────────────────────────────────────── */

/*
 * Global thread pool — lazily initialised on first async spawn.
 *
 * Pool size defaults to the online CPU count (capped at 16, minimum 2).
 * Call duxrt_pool_set_size() before the first async spawn to override.
 */
static void*          g_pool      = NULL;
static pthread_once_t g_pool_once = PTHREAD_ONCE_INIT;
static int32_t        g_pool_size = 0;  /* 0 = use CPU count */

static void init_global_pool(void) {
    int32_t n = g_pool_size > 0 ? g_pool_size : 4;
    /* Try to get CPU count */
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus > 0 && g_pool_size == 0) n = (int32_t)cpus;
    if (n < 2)  n = 2;
    if (n > 16) n = 16;   /* cap at 16 */
    g_pool = duxrt_pool_new(n);
}

/* Configure pool size before first async spawn (optional). */
void duxrt_pool_set_size(int32_t n) {
    g_pool_size = n;
}

/* ── Spawn ───────────────────────────────────────────────────────────────── */

/*
 * Submit fn(env) to the global thread pool.
 * fn must be a pthread-compatible worker: (void*) -> void*
 * Completion is signalled via the future (duxrt_future_set).
 *
 * Previously this created a new detached pthread per call; now it reuses the
 * fixed pool, preventing thread exhaustion under load.
 */
void duxrt_future_spawn_detached(void* fn, void* env) {
    pthread_once(&g_pool_once, init_global_pool);
    duxrt_pool_submit(g_pool, fn, env);
}
