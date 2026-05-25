/*
 * process_rt.c — process management runtime for Dux stdlib sys.process
 */
#include "duxrt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

int64_t duxrt_process_run(DuxStr* cmd) {
    if (!cmd) return -1;
    return (int64_t)system(duxrt_str_cstr(cmd));
}

DuxStr* duxrt_process_capture(DuxStr* cmd) {
    if (!cmd) return duxrt_str_new("", 0);
    FILE* f = popen(duxrt_str_cstr(cmd), "r");
    if (!f) return duxrt_str_new("", 0);

    /* Build up result string by concatenating chunks */
    DuxStr* result = duxrt_str_new("", 0);
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        DuxStr* chunk = duxrt_str_new(buf, (int64_t)strlen(buf));
        DuxStr* concat = duxrt_str_concat(result, chunk);
        duxrt_str_release(result);
        duxrt_str_release(chunk);
        result = concat;
    }
    pclose(f);
    return result;
}

int64_t duxrt_process_pid(void) {
    return (int64_t)getpid();
}

int64_t duxrt_process_ppid(void) {
    return (int64_t)getppid();
}

int64_t duxrt_process_wait(int64_t pid) {
    int status = 0;
    if (waitpid((pid_t)pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? (int64_t)WEXITSTATUS(status) : -1;
}

int64_t duxrt_process_spawn(DuxStr* cmd, DuxList* args_list) {
    if (!cmd) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        /* child */
        int64_t n = duxrt_list_len(args_list);
        char** argv = (char**)malloc(sizeof(char*) * (size_t)(n + 2));
        if (!argv) _exit(1);
        argv[0] = (char*)duxrt_str_cstr(cmd);
        for (int64_t i = 0; i < n; i++) {
            DuxStr* s = (DuxStr*)duxrt_list_get(args_list, i);
            argv[i + 1] = (char*)duxrt_str_cstr(s);
        }
        argv[n + 1] = NULL;
        execvp(argv[0], argv);
        _exit(127);
    }
    return (int64_t)pid;
}

/* ── Process handle API ─────────────────────────────────────────────────── */

#include <fcntl.h>

typedef struct DuxProc {
    pid_t    pid;
    int      cap_out;    /* capturing stdout? */
    int      cap_err;    /* capturing stderr? */
    int      out_fd;     /* read end of stdout pipe */
    int      err_fd;     /* read end of stderr pipe */
    DuxStr*  out_buf;    /* captured stdout (after wait) */
    DuxStr*  err_buf;    /* captured stderr (after wait) */
    int      exit_code;
    int      waited;
} DuxProc;

static DuxStr* read_fd_to_str(int fd) {
    if (fd < 0) return duxrt_str_new("", 0);
    char buf[4096];
    size_t total = 0;
    char* acc = NULL;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        acc = (char*)realloc(acc, total + (size_t)n + 1);
        if (!acc) break;
        memcpy(acc + total, buf, (size_t)n);
        total += (size_t)n;
    }
    close(fd);
    if (!acc) return duxrt_str_new("", 0);
    acc[total] = '\0';
    DuxStr* s = duxrt_str_new(acc, (int64_t)total);
    free(acc);
    return s;
}

void* duxrt_proc_spawn(DuxList* argv_list, int32_t cap_out, int32_t cap_err) {
    if (!argv_list || argv_list->len == 0) return NULL;
    DuxProc* p = (DuxProc*)calloc(1, sizeof(DuxProc));
    if (!p) return NULL;
    p->cap_out  = cap_out;
    p->cap_err  = cap_err;
    p->out_fd   = -1;
    p->err_fd   = -1;

    /* Build argv (null-terminated char* array) */
    int argc = (int)argv_list->len;
    char** argv = (char**)malloc((size_t)(argc + 1) * sizeof(char*));
    if (!argv) { free(p); return NULL; }
    for (int i = 0; i < argc; i++)
        argv[i] = (char*)duxrt_str_cstr((DuxStr*)((void**)argv_list->data)[i]);
    argv[argc] = NULL;

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (cap_out) pipe(out_pipe);
    if (cap_err) pipe(err_pipe);

    pid_t pid = fork();
    if (pid < 0) {
        if (cap_out) { close(out_pipe[0]); close(out_pipe[1]); }
        if (cap_err) { close(err_pipe[0]); close(err_pipe[1]); }
        free(argv); free(p); return NULL;
    }
    if (pid == 0) {
        /* child */
        if (cap_out) { dup2(out_pipe[1], STDOUT_FILENO); close(out_pipe[0]); close(out_pipe[1]); }
        if (cap_err) { dup2(err_pipe[1], STDERR_FILENO); close(err_pipe[0]); close(err_pipe[1]); }
        execvp(argv[0], argv);
        _exit(127);
    }
    /* parent */
    if (cap_out) { close(out_pipe[1]); p->out_fd = out_pipe[0]; }
    if (cap_err) { close(err_pipe[1]); p->err_fd = err_pipe[0]; }
    free(argv);
    p->pid = pid;
    return p;
}

int32_t duxrt_proc_wait(void* proc) {
    DuxProc* p = (DuxProc*)proc;
    if (!p || p->waited) return p ? p->exit_code : -1;
    /* read captured output before waitpid to avoid pipe deadlock */
    if (p->cap_out) p->out_buf = read_fd_to_str(p->out_fd);
    if (p->cap_err) p->err_buf = read_fd_to_str(p->err_fd);
    p->out_fd = -1;
    p->err_fd = -1;
    int status = 0;
    waitpid(p->pid, &status, 0);
    p->waited = 1;
    if (WIFEXITED(status))        p->exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) p->exit_code = -WTERMSIG(status);
    else p->exit_code = -1;
    return (int32_t)p->exit_code;
}

DuxStr* duxrt_proc_stdout(void* proc) {
    DuxProc* p = (DuxProc*)proc;
    if (!p) return duxrt_str_new("", 0);
    if (p->out_buf) return p->out_buf;
    return duxrt_str_new("", 0);
}

DuxStr* duxrt_proc_stderr(void* proc) {
    DuxProc* p = (DuxProc*)proc;
    if (!p) return duxrt_str_new("", 0);
    if (p->err_buf) return p->err_buf;
    return duxrt_str_new("", 0);
}

int32_t duxrt_proc_kill(void* proc, int32_t sig) {
    DuxProc* p = (DuxProc*)proc;
    if (!p) return -1;
    return kill(p->pid, (int)sig);
}

void duxrt_proc_free(void* proc) {
    DuxProc* p = (DuxProc*)proc;
    if (!p) return;
    if (!p->waited) {
        if (p->out_fd >= 0) { close(p->out_fd); }
        if (p->err_fd >= 0) { close(p->err_fd); }
        int status;
        waitpid(p->pid, &status, 0);
    }
    free(p);
}
