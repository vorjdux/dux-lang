/*
 * async_rt.c — cooperative async scheduler runtime (Phase 1)
 *
 * Phase 1 provides:
 *   - sleep_ms / yield  (time-based cooperative yield)
 *   - run / stop        (event-loop control)
 *
 * Phase 2 will add async/await keywords and LLVM coroutine lowering.
 * All public symbols are prefixed duxrt_async_ to avoid collisions.
 */
#include "duxrt.h"
#include <sched.h>
#include <time.h>
#include <stdatomic.h>

/* ── Global event-loop state ─────────────────────────────────────────────── */

static _Atomic int g_stop_requested = 0;
static _Atomic int g_running        = 0;

/* ── Yield / sleep ───────────────────────────────────────────────────────── */

void duxrt_async_sleep_ms(int64_t ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void duxrt_async_yield(void) {
    sched_yield();
}

/* ── Event loop ──────────────────────────────────────────────────────────── */

/*
 * run() — enter the event loop.
 * Ticks every 1 ms until stop() is called.
 * If stop() was called *before* run(), run() exits on the first check.
 */
void duxrt_async_run(void) {
    atomic_store(&g_running, 1);

    struct timespec tick = {0, 1000000L}; /* 1 ms per tick */
    while (!atomic_load(&g_stop_requested)) {
        nanosleep(&tick, NULL);
    }

    atomic_store(&g_running, 0);
    atomic_store(&g_stop_requested, 0); /* reset so run() can be called again */
}

/*
 * stop() — signal the event loop to exit.
 * Safe to call before run() — run() will then return immediately.
 */
void duxrt_async_stop(void) {
    atomic_store(&g_stop_requested, 1);
    atomic_store(&g_running, 0);
}

/* Returns 1 while the event loop is executing duxrt_async_run(). */
int32_t duxrt_async_is_running(void) {
    return atomic_load(&g_running) ? 1 : 0;
}
