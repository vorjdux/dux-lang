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
