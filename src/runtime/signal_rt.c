/*
 * signal_rt.c — signal handling runtime for Dux stdlib sys.signal
 */
#include "duxrt.h"
#include <signal.h>
#include <sys/types.h>

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
