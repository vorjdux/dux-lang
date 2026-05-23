/*
 * signal_rt.c — signal handling runtime for Dux stdlib sys.signal
 */
#include "duxrt.h"
#include <signal.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>

int64_t duxrt_signal_sigint(void)  { return (int64_t)SIGINT;  }
int64_t duxrt_signal_sigterm(void) { return (int64_t)SIGTERM; }
int64_t duxrt_signal_sighup(void)  { return (int64_t)SIGHUP;  }
int64_t duxrt_signal_sigusr1(void) { return (int64_t)SIGUSR1; }
int64_t duxrt_signal_sigusr2(void) { return (int64_t)SIGUSR2; }
int64_t duxrt_signal_sigchld(void) { return (int64_t)SIGCHLD; }
int64_t duxrt_signal_sigpipe(void) { return (int64_t)SIGPIPE; }
int64_t duxrt_signal_sigalrm(void) { return (int64_t)SIGALRM; }

int32_t duxrt_signal_ignore(int64_t sig) {
    return signal((int)sig, SIG_IGN) != SIG_ERR ? 1 : 0;
}

int32_t duxrt_signal_reset(int64_t sig) {
    return signal((int)sig, SIG_DFL) != SIG_ERR ? 1 : 0;
}

int32_t duxrt_signal_raise(int64_t sig) {
    return raise((int)sig) == 0 ? 1 : 0;
}

int32_t duxrt_signal_kill(int64_t pid, int64_t sig) {
    return kill((pid_t)pid, (int)sig) == 0 ? 1 : 0;
}

/* ── Signal handler registration ─────────────────────────────────────────── */

typedef void (*dux_sig_fn_t)(void);

static dux_sig_fn_t g_sig_handlers[64] = {NULL};

static void sig_trampoline(int sig) {
    if (sig >= 0 && sig < 64 && g_sig_handlers[sig])
        g_sig_handlers[sig]();
}

void duxrt_signal_register(int32_t signum, void* fn) {
    if (signum <= 0 || signum >= 64) return;
    g_sig_handlers[signum] = (dux_sig_fn_t)fn;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_trampoline;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction((int)signum, &sa, NULL);
}

void duxrt_signal_block(int32_t signum) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, (int)signum);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

void duxrt_signal_unblock(int32_t signum) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, (int)signum);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}
