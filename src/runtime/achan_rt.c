/*
 * achan_rt.c — non-blocking ring-buffer async channels
 *
 * AsyncChan is a fixed-capacity ring buffer protected by a mutex.
 * - try_send / try_recv are non-blocking (return immediately).
 * - send / recv are blocking (wait using condvars).
 * - Sending to a closed channel is a no-op.
 * - Receiving from a closed, empty channel returns NULL.
 *
 * All public symbols are prefixed duxrt_achan_ to avoid collisions with
 * the blocking thread channels (duxrt_chan_*).
 */
#include "duxrt.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct AchanBuf {
    void**          buf;        /* ring-buffer storage          */
    int64_t         cap;        /* buffer capacity (> 0)        */
    int64_t         head;       /* next write slot              */
    int64_t         tail;       /* next read  slot              */
    int64_t         len;        /* number of items in buffer    */
    int             closed;
    pthread_mutex_t mu;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} AchanBuf;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

void* duxrt_achan_new(int64_t cap) {
    if (cap <= 0) cap = 1;
    AchanBuf* c = (AchanBuf*)calloc(1, sizeof(AchanBuf));
    if (!c) return NULL;
    c->buf = (void**)calloc((size_t)cap, sizeof(void*));
    if (!c->buf) { free(c); return NULL; }
    c->cap = cap;
    pthread_mutex_init(&c->mu, NULL);
    pthread_cond_init(&c->not_empty, NULL);
    pthread_cond_init(&c->not_full, NULL);
    return c;
}

void duxrt_achan_free(void* ch) {
    AchanBuf* c = (AchanBuf*)ch;
    if (!c) return;
    pthread_mutex_destroy(&c->mu);
    pthread_cond_destroy(&c->not_empty);
    pthread_cond_destroy(&c->not_full);
    free(c->buf);
    free(c);
}

/* ── Close ───────────────────────────────────────────────────────────────── */

void duxrt_achan_close(void* ch) {
    AchanBuf* c = (AchanBuf*)ch;
    if (!c) return;
    pthread_mutex_lock(&c->mu);
    c->closed = 1;
    pthread_cond_broadcast(&c->not_empty);
    pthread_cond_broadcast(&c->not_full);
    pthread_mutex_unlock(&c->mu);
}

int32_t duxrt_achan_is_closed(void* ch) {
    AchanBuf* c = (AchanBuf*)ch;
    if (!c) return 1;
    pthread_mutex_lock(&c->mu);
    int r = c->closed;
    pthread_mutex_unlock(&c->mu);
    return r;
}

/* ── Capacity / length ───────────────────────────────────────────────────── */

int64_t duxrt_achan_len(void* ch) {
    AchanBuf* c = (AchanBuf*)ch;
    if (!c) return 0;
    pthread_mutex_lock(&c->mu);
    int64_t n = c->len;
    pthread_mutex_unlock(&c->mu);
    return n;
}

int64_t duxrt_achan_cap(void* ch) {
    AchanBuf* c = (AchanBuf*)ch;
    if (!c) return 0;
    return c->cap; /* immutable — no lock needed */
}

/* ── Non-blocking send/recv ──────────────────────────────────────────────── */

/* Returns 1 if the item was enqueued, 0 if the channel is full or closed. */
int32_t duxrt_achan_try_send(void* ch, void* val) {
    AchanBuf* c = (AchanBuf*)ch;
    if (!c) return 0;
    pthread_mutex_lock(&c->mu);
    if (c->closed || c->len >= c->cap) {
        pthread_mutex_unlock(&c->mu);
        return 0;
    }
    c->buf[c->head] = val;
    c->head = (c->head + 1) % c->cap;
    c->len++;
    pthread_cond_signal(&c->not_empty);
    pthread_mutex_unlock(&c->mu);
    return 1;
}

/* Returns the next item, or NULL if the channel is empty. */
void* duxrt_achan_try_recv(void* ch) {
    AchanBuf* c = (AchanBuf*)ch;
    if (!c) return NULL;
    pthread_mutex_lock(&c->mu);
    if (c->len == 0) {
        pthread_mutex_unlock(&c->mu);
        return NULL;
    }
    void* val = c->buf[c->tail];
    c->tail = (c->tail + 1) % c->cap;
    c->len--;
    pthread_cond_signal(&c->not_full);
    pthread_mutex_unlock(&c->mu);
    return val;
}

/* ── Blocking send/recv ──────────────────────────────────────────────────── */

/* Blocks until space is available (or channel is closed). */
void duxrt_achan_send(void* ch, void* val) {
    AchanBuf* c = (AchanBuf*)ch;
    if (!c) return;
    pthread_mutex_lock(&c->mu);
    while (!c->closed && c->len >= c->cap)
        pthread_cond_wait(&c->not_full, &c->mu);
    if (!c->closed) {
        c->buf[c->head] = val;
        c->head = (c->head + 1) % c->cap;
        c->len++;
        pthread_cond_signal(&c->not_empty);
    }
    pthread_mutex_unlock(&c->mu);
}

/* Blocks until an item is available. Returns NULL if closed+empty. */
void* duxrt_achan_recv(void* ch) {
    AchanBuf* c = (AchanBuf*)ch;
    if (!c) return NULL;
    pthread_mutex_lock(&c->mu);
    while (c->len == 0 && !c->closed)
        pthread_cond_wait(&c->not_empty, &c->mu);
    if (c->len == 0) {
        pthread_mutex_unlock(&c->mu);
        return NULL;
    }
    void* val = c->buf[c->tail];
    c->tail = (c->tail + 1) % c->cap;
    c->len--;
    pthread_cond_signal(&c->not_full);
    pthread_mutex_unlock(&c->mu);
    return val;
}
