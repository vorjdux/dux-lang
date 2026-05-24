/*
 * tls_rt.c — thread-local storage (TLS) runtime support for Dux
 *
 * Primitive thread_local variables (int, long, double, bool, str) are
 * handled directly by LLVM via the thread_local global attribute.
 * This file provides hooks for future object-typed TLS destructors.
 */
#include "duxrt.h"
#include <pthread.h>

/* ── Thread-local storage (TLS) ──────────────────────────────────────────── */

void duxrt_tls_register_dtor(void* key, void (*dtor)(void*)) {
    /* Follow-up: register per-thread destructor via pthread_key_t */
    (void)key; (void)dtor;
}
