/*
 * pool_rt.c — DuxPool: fixed-size POSIX thread pool
 *
 * Provides a bounded set of worker threads that execute submitted tasks from
 * a shared FIFO queue.  Replaces the old model of spawning a new detached
 * pthread per async call, which exhausted OS resources under load.
 *
 * Public API (declared in duxrt.h):
 *   duxrt_pool_new(n)           — create pool with n workers
 *   duxrt_pool_submit(p,fn,arg) — enqueue a task (non-blocking)
 *   duxrt_pool_wait(p)          — block until all pending tasks finish
 *   duxrt_pool_shutdown(p)      — signal workers to stop and join them
 *   duxrt_pool_free(p)          — shutdown + destroy synchronisation objects
 */
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "duxrt.h"

/* ── Task queue entry ─────────────────────────────────── */
typedef struct Task {
    void* (*fn)(void*);
    void*        arg;
    struct Task* next;
} Task;

/* ── Thread pool ──────────────────────────────────────── */
typedef struct DuxPool {
    pthread_t*       threads;
    int32_t          n_threads;
    Task*            head;
    Task*            tail;
    pthread_mutex_t  mu;
    pthread_cond_t   not_empty;
    int              shutdown;
    int              pending;          /* tasks queued or running */
    pthread_cond_t   all_done;
} DuxPool;

static void* worker(void* arg) {
    DuxPool* p = (DuxPool*)arg;
    for (;;) {
        pthread_mutex_lock(&p->mu);
        while (!p->shutdown && p->head == NULL)
            pthread_cond_wait(&p->not_empty, &p->mu);
        if (p->shutdown && p->head == NULL) {
            pthread_mutex_unlock(&p->mu);
            return NULL;
        }
        Task* t = p->head;
        p->head = t->next;
        if (!p->head) p->tail = NULL;
        pthread_mutex_unlock(&p->mu);
        t->fn(t->arg);
        free(t);
        pthread_mutex_lock(&p->mu);
        p->pending--;
        if (p->pending == 0) pthread_cond_broadcast(&p->all_done);
        pthread_mutex_unlock(&p->mu);
    }
}

void* duxrt_pool_new(int32_t n) {
    if (n <= 0) n = 4;
    DuxPool* p = calloc(1, sizeof(DuxPool));
    p->n_threads = n;
    p->threads   = malloc((size_t)n * sizeof(pthread_t));
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->not_empty, NULL);
    pthread_cond_init(&p->all_done, NULL);
    for (int32_t i = 0; i < n; i++)
        pthread_create(&p->threads[i], NULL, worker, p);
    return p;
}

void duxrt_pool_submit(void* pool, void* fn_ptr, void* arg) {
    DuxPool* p = (DuxPool*)pool;
    Task* t = malloc(sizeof(Task));
    t->fn   = __extension__ (void*(*)(void*))fn_ptr;
    t->arg  = arg;
    t->next = NULL;
    pthread_mutex_lock(&p->mu);
    p->pending++;
    if (p->tail) p->tail->next = t; else p->head = t;
    p->tail = t;
    pthread_cond_signal(&p->not_empty);
    pthread_mutex_unlock(&p->mu);
}

void duxrt_pool_wait(void* pool) {
    DuxPool* p = (DuxPool*)pool;
    pthread_mutex_lock(&p->mu);
    while (p->pending > 0)
        pthread_cond_wait(&p->all_done, &p->mu);
    pthread_mutex_unlock(&p->mu);
}

void duxrt_pool_shutdown(void* pool) {
    DuxPool* p = (DuxPool*)pool;
    pthread_mutex_lock(&p->mu);
    p->shutdown = 1;
    pthread_cond_broadcast(&p->not_empty);
    pthread_mutex_unlock(&p->mu);
    for (int32_t i = 0; i < p->n_threads; i++)
        pthread_join(p->threads[i], NULL);
}

void duxrt_pool_free(void* pool) {
    DuxPool* p = (DuxPool*)pool;
    duxrt_pool_shutdown(p);
    pthread_mutex_destroy(&p->mu);
    pthread_cond_destroy(&p->not_empty);
    pthread_cond_destroy(&p->all_done);
    free(p->threads);
    free(p);
}
