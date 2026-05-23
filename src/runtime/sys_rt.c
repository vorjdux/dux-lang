/*
 * sys_rt.c — system / environment / args runtime for Dux stdlib sys.*
 */
#include "duxrt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* argc/argv saved at startup */
static int    g_argc = 0;
static char** g_argv = NULL;

void duxrt_sys_init(int argc, char** argv) {
    g_argc = argc;
    g_argv = argv;
}

void duxrt_sys_exit(int64_t code) {
    exit((int)code);
}

/* ── env ─────────────────────────────────────────────────────────────────── */

DuxStr* duxrt_env_fetch(DuxStr* name) {
    if (!name) return duxrt_str_new("", 0);
    const char* val = getenv(duxrt_str_cstr(name));
    if (!val) return duxrt_str_new("", 0);
    return duxrt_str_new(val, (int64_t)strlen(val));
}

int32_t duxrt_env_has(DuxStr* name) {
    if (!name) return 0;
    return getenv(duxrt_str_cstr(name)) != NULL ? 1 : 0;
}

int32_t duxrt_env_store(DuxStr* name, DuxStr* val) {
    if (!name || !val) return 0;
    return setenv(duxrt_str_cstr(name), duxrt_str_cstr(val), 1) == 0 ? 1 : 0;
}

int32_t duxrt_env_remove(DuxStr* name) {
    if (!name) return 0;
    return unsetenv(duxrt_str_cstr(name)) == 0 ? 1 : 0;
}

/* ── args ────────────────────────────────────────────────────────────────── */

int64_t duxrt_args_count(void) {
    return (int64_t)g_argc;
}

DuxStr* duxrt_args_at(int64_t i) {
    if (i < 0 || i >= g_argc) return duxrt_str_new("", 0);
    return duxrt_str_new(g_argv[i], (int64_t)strlen(g_argv[i]));
}

DuxList* duxrt_args_all(void) {
    DuxList* list = duxrt_list_new();
    for (int i = 0; i < g_argc; i++) {
        duxrt_list_push(list, duxrt_str_new(g_argv[i], (int64_t)strlen(g_argv[i])));
    }
    return list;
}

/* ── sys ─────────────────────────────────────────────────────────────────── */

int64_t duxrt_sys_getpid(void) {
    return (int64_t)getpid();
}

int64_t duxrt_sys_getppid(void) {
    return (int64_t)getppid();
}

DuxStr* duxrt_sys_hostname(void) {
    char buf[256];
    if (gethostname(buf, sizeof(buf)) != 0) return duxrt_str_new("", 0);
    return duxrt_str_new(buf, (int64_t)strlen(buf));
}
