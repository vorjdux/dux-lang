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

typedef struct {
    pthread_t tid;
    int       joined;
} DuxThread;

void* duxrt_thread_spawn(void* fn, void* arg) {
    DuxThread* t = (DuxThread*)malloc(sizeof(DuxThread));
    if (!t) return NULL;
    t->joined = 0;
    ThreadArgs* ta = (ThreadArgs*)malloc(sizeof(ThreadArgs));
    if (!ta) { free(t); return NULL; }
    ta->fn  = (void (*)(void*))fn;
    ta->arg = arg;
    if (pthread_create(&t->tid, NULL, thread_runner, ta) != 0) {
        free(ta); free(t); return NULL;
    }
    return t;
}

int32_t duxrt_thread_join(void* handle) {
    if (!handle) return -1;
    DuxThread* t = (DuxThread*)handle;
    if (t->joined) return -1;
    int r = pthread_join(t->tid, NULL);
    if (r == 0) t->joined = 1;
    return r == 0 ? 0 : -1;
}

int32_t duxrt_thread_detach(void* handle) {
    if (!handle) return -1;
    DuxThread* t = (DuxThread*)handle;
    if (t->joined) return -1;
    int r = pthread_detach(t->tid);
    if (r == 0) t->joined = 1;
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
    pthread_mutex_t mu;
    int             done;
    void           (*fn)(void);
} DuxOnce;

void* duxrt_once_new(void) {
    DuxOnce* o = (DuxOnce*)malloc(sizeof(DuxOnce));
    if (!o) return NULL;
    pthread_mutex_init(&o->mu, NULL);
    o->done = 0;
    o->fn   = NULL;
    return o;
}

void duxrt_once_call(void* handle, void* fn) {
    DuxOnce* o = (DuxOnce*)handle;
    if (!o || !fn) return;
    pthread_mutex_lock(&o->mu);
    if (!o->done) {
        o->fn = (void (*)(void))fn;
        o->fn();
        o->done = 1;
    }
    pthread_mutex_unlock(&o->mu);
}

void duxrt_once_free(void* handle) {
    if (!handle) return;
    DuxOnce* o = (DuxOnce*)handle;
    pthread_mutex_destroy(&o->mu);
    free(o);
}

/* ── Thread pool ─────────────────────────────────────────────────────────── */

typedef struct DuxPoolTask {
    void (*fn)(void*);
    void*              arg;
    struct DuxPoolTask* next;
} DuxPoolTask;

typedef struct DuxPool {
    pthread_t*      workers;
    int32_t         n_workers;
    pthread_mutex_t mu;
    pthread_cond_t  not_empty;
    pthread_cond_t  all_done;
    DuxPoolTask*    head;
    DuxPoolTask*    tail;
    int32_t         pending;
    int32_t         shutdown;
} DuxPool;

static void* pool_worker(void* arg) {
    DuxPool* p = (DuxPool*)arg;
    for (;;) {
        pthread_mutex_lock(&p->mu);
        while (!p->head && !p->shutdown)
            pthread_cond_wait(&p->not_empty, &p->mu);
        if (p->shutdown && !p->head) {
            pthread_mutex_unlock(&p->mu);
            break;
        }
        DuxPoolTask* t = p->head;
        p->head = t->next;
        if (!p->head) p->tail = NULL;
        pthread_mutex_unlock(&p->mu);

        t->fn(t->arg);
        free(t);

        pthread_mutex_lock(&p->mu);
        p->pending--;
        if (p->pending == 0)
            pthread_cond_broadcast(&p->all_done);
        pthread_mutex_unlock(&p->mu);
    }
    return NULL;
}

void* duxrt_pool_new(int32_t n) {
    if (n <= 0) n = 1;
    DuxPool* p = (DuxPool*)calloc(1, sizeof(DuxPool));
    if (!p) return NULL;
    p->n_workers = n;
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->not_empty, NULL);
    pthread_cond_init(&p->all_done, NULL);
    p->workers = (pthread_t*)malloc((size_t)n * sizeof(pthread_t));
    if (!p->workers) { free(p); return NULL; }
    for (int i = 0; i < n; i++)
        pthread_create(&p->workers[i], NULL, pool_worker, p);
    return p;
}

void duxrt_pool_submit(void* pool, void* fn_ptr, void* arg) {
    DuxPool* p = (DuxPool*)pool;
    if (!p || !fn_ptr) return;
    DuxPoolTask* t = (DuxPoolTask*)malloc(sizeof(DuxPoolTask));
    if (!t) return;
    t->fn   = (void (*)(void*))fn_ptr;
    t->arg  = arg;
    t->next = NULL;
    pthread_mutex_lock(&p->mu);
    if (p->tail) p->tail->next = t; else p->head = t;
    p->tail = t;
    p->pending++;
    pthread_cond_signal(&p->not_empty);
    pthread_mutex_unlock(&p->mu);
}

void duxrt_pool_wait(void* pool) {
    DuxPool* p = (DuxPool*)pool;
    if (!p) return;
    pthread_mutex_lock(&p->mu);
    while (p->pending > 0)
        pthread_cond_wait(&p->all_done, &p->mu);
    pthread_mutex_unlock(&p->mu);
}

void duxrt_pool_shutdown(void* pool) {
    DuxPool* p = (DuxPool*)pool;
    if (!p) return;
    pthread_mutex_lock(&p->mu);
    p->shutdown = 1;
    pthread_cond_broadcast(&p->not_empty);
    pthread_mutex_unlock(&p->mu);
    for (int i = 0; i < p->n_workers; i++)
        pthread_join(p->workers[i], NULL);
}

void duxrt_pool_free(void* pool) {
    DuxPool* p = (DuxPool*)pool;
    if (!p) return;
    DuxPoolTask* t = p->head;
    while (t) { DuxPoolTask* next = t->next; free(t); t = next; }
    pthread_mutex_destroy(&p->mu);
    pthread_cond_destroy(&p->not_empty);
    pthread_cond_destroy(&p->all_done);
    free(p->workers);
    free(p);
}

/* ── Channel ─────────────────────────────────────────────────────────────── */

typedef struct DuxChanNode {
    void*             val;
    struct DuxChanNode* next;
} DuxChanNode;

typedef struct DuxChan {
    pthread_mutex_t mu;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
    DuxChanNode*    head;
    DuxChanNode*    tail;
    int64_t         len;
    int64_t         cap;   /* 0 = unbounded */
    int32_t         closed;
} DuxChan;

void* duxrt_chan_new(int64_t cap) {
    DuxChan* c = (DuxChan*)calloc(1, sizeof(DuxChan));
    if (!c) return NULL;
    pthread_mutex_init(&c->mu, NULL);
    pthread_cond_init(&c->not_empty, NULL);
    pthread_cond_init(&c->not_full, NULL);
    c->cap = cap;
    return c;
}

void duxrt_chan_send(void* ch, void* val) {
    DuxChan* c = (DuxChan*)ch;
    if (!c) return;
    pthread_mutex_lock(&c->mu);
    /* bounded: wait while full and not closed */
    while (c->cap > 0 && c->len >= c->cap && !c->closed)
        pthread_cond_wait(&c->not_full, &c->mu);
    if (c->closed) { pthread_mutex_unlock(&c->mu); return; }
    DuxChanNode* node = (DuxChanNode*)malloc(sizeof(DuxChanNode));
    if (!node) { pthread_mutex_unlock(&c->mu); return; }
    node->val  = val;
    node->next = NULL;
    if (c->tail) c->tail->next = node; else c->head = node;
    c->tail = node;
    c->len++;
    pthread_cond_signal(&c->not_empty);
    pthread_mutex_unlock(&c->mu);
}

void* duxrt_chan_recv(void* ch) {
    DuxChan* c = (DuxChan*)ch;
    if (!c) return NULL;
    pthread_mutex_lock(&c->mu);
    while (!c->head && !c->closed)
        pthread_cond_wait(&c->not_empty, &c->mu);
    if (!c->head) { pthread_mutex_unlock(&c->mu); return NULL; }
    DuxChanNode* node = c->head;
    c->head = node->next;
    if (!c->head) c->tail = NULL;
    c->len--;
    void* val = node->val;
    free(node);
    pthread_cond_signal(&c->not_full);
    pthread_mutex_unlock(&c->mu);
    return val;
}

/* 1 = got item, 0 = empty+open, -1 = closed+empty */
int32_t duxrt_chan_try_recv(void* ch, void** out) {
    DuxChan* c = (DuxChan*)ch;
    if (!c) return -1;
    pthread_mutex_lock(&c->mu);
    if (!c->head) {
        int r = c->closed ? -1 : 0;
        pthread_mutex_unlock(&c->mu);
        return r;
    }
    DuxChanNode* node = c->head;
    c->head = node->next;
    if (!c->head) c->tail = NULL;
    c->len--;
    if (out) *out = node->val;
    free(node);
    pthread_cond_signal(&c->not_full);
    pthread_mutex_unlock(&c->mu);
    return 1;
}

void duxrt_chan_close(void* ch) {
    DuxChan* c = (DuxChan*)ch;
    if (!c) return;
    pthread_mutex_lock(&c->mu);
    c->closed = 1;
    pthread_cond_broadcast(&c->not_empty);
    pthread_cond_broadcast(&c->not_full);
    pthread_mutex_unlock(&c->mu);
}

int32_t duxrt_chan_is_closed(void* ch) {
    DuxChan* c = (DuxChan*)ch;
    if (!c) return 1;
    pthread_mutex_lock(&c->mu);
    int r = c->closed;
    pthread_mutex_unlock(&c->mu);
    return r;
}

int64_t duxrt_chan_len(void* ch) {
    DuxChan* c = (DuxChan*)ch;
    if (!c) return 0;
    pthread_mutex_lock(&c->mu);
    int64_t n = c->len;
    pthread_mutex_unlock(&c->mu);
    return n;
}

void duxrt_chan_free(void* ch) {
    DuxChan* c = (DuxChan*)ch;
    if (!c) return;
    DuxChanNode* node = c->head;
    while (node) { DuxChanNode* next = node->next; free(node); node = next; }
    pthread_mutex_destroy(&c->mu);
    pthread_cond_destroy(&c->not_empty);
    pthread_cond_destroy(&c->not_full);
    free(c);
}
