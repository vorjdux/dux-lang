/*
 * duxrt — signal handling runtime
 *
 * Provides signal registration for the sys.signal stdlib module.
 * Handlers are simple function pointers (void→void) registered via
 * the POSIX sigaction API.
 */
#define _GNU_SOURCE
#include "duxrt.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>

/* ── Signal constants ────────────────────────────────────────────────────── */

int64_t duxrt_signal_sigint(void)  { return SIGINT;  }
int64_t duxrt_signal_sigterm(void) { return SIGTERM; }
int64_t duxrt_signal_sighup(void)  { return SIGHUP;  }
int64_t duxrt_signal_sigusr1(void) { return SIGUSR1; }
int64_t duxrt_signal_sigusr2(void) { return SIGUSR2; }
int64_t duxrt_signal_sigchld(void) { return SIGCHLD; }
int64_t duxrt_signal_sigpipe(void) { return SIGPIPE; }
int64_t duxrt_signal_sigalrm(void) { return SIGALRM; }

/* ── Register a handler (void(void) fn ptr) ──────────────────────────────── */

int duxrt_signal_handle(int64_t signum, void (*handler)(void)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = (void (*)(int))handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    return sigaction((int)signum, &sa, NULL) == 0 ? 1 : 0;
}

/* ── Ignore a signal ─────────────────────────────────────────────────────── */

int duxrt_signal_ignore(int64_t signum) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    return sigaction((int)signum, &sa, NULL) == 0 ? 1 : 0;
}

/* ── Restore default handler ─────────────────────────────────────────────── */

int duxrt_signal_reset(int64_t signum) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    return sigaction((int)signum, &sa, NULL) == 0 ? 1 : 0;
}

/* ── Raise a signal to the current process ───────────────────────────────── */

int duxrt_signal_raise(int64_t signum) {
    return raise((int)signum) == 0 ? 1 : 0;
}

/* ── Send a signal to a process ──────────────────────────────────────────── */

int duxrt_signal_kill(int64_t pid, int64_t signum) {
    return kill((pid_t)pid, (int)signum) == 0 ? 1 : 0;
}
