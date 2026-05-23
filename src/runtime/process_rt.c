/*
 * duxrt — process spawning runtime
 *
 * Provides process creation and management for the sys.process stdlib module.
 */
#define _GNU_SOURCE
#include "duxrt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

/* ── Run a shell command and return exit code ────────────────────────────── */

int64_t duxrt_process_run(DuxStr* cmd) {
    const char* c = duxrt_str_cstr(cmd);
    if (!c) return -1;
    return (int64_t)system(c);
}

/* ── Run and capture stdout ──────────────────────────────────────────────── */

DuxStr* duxrt_process_capture(DuxStr* cmd) {
    const char* c = duxrt_str_cstr(cmd);
    if (!c) return duxrt_str_new("", 0);

    FILE* f = popen(c, "r");
    if (!f) return duxrt_str_new("", 0);

    /* Read all output */
    char* buf = NULL;
    size_t total = 0;
    char tmp[4096];
    size_t n;
    while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) {
        char* new_buf = (char*)realloc(buf, total + n + 1);
        if (!new_buf) break;
        buf = new_buf;
        memcpy(buf + total, tmp, n);
        total += n;
    }
    pclose(f);
    if (!buf) return duxrt_str_new("", 0);
    buf[total] = '\0';
    DuxStr* result = duxrt_str_new(buf, (int64_t)total);
    free(buf);
    return result;
}

/* ── Get current process ID ──────────────────────────────────────────────── */

int64_t duxrt_process_pid(void) {
    return (int64_t)getpid();
}

/* ── Get parent process ID ───────────────────────────────────────────────── */

int64_t duxrt_process_ppid(void) {
    return (int64_t)getppid();
}

/* ── Fork and exec (simple version) ─────────────────────────────────────── */

int64_t duxrt_process_spawn(DuxStr* prog, DuxList* args) {
    const char* p = duxrt_str_cstr(prog);
    if (!p) return -1;

    int64_t argc = args ? duxrt_list_len(args) : 0;
    char** argv = (char**)malloc(sizeof(char*) * (size_t)(argc + 2));
    if (!argv) return -1;

    argv[0] = (char*)p;
    for (int64_t i = 0; i < argc; ++i) {
        DuxStr* s = (DuxStr*)duxrt_list_get(args, i);
        argv[i + 1] = (char*)duxrt_str_cstr(s);
    }
    argv[argc + 1] = NULL;

    pid_t pid = fork();
    if (pid < 0) { free(argv); return -1; }
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    free(argv);
    return (int64_t)pid;
}

/* ── Wait for a process to finish ────────────────────────────────────────── */

int64_t duxrt_process_wait(int64_t pid) {
    int status = 0;
    if (waitpid((pid_t)pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? (int64_t)WEXITSTATUS(status) : -1;
}
