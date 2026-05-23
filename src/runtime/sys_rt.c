/*
 * duxrt — system environment and args runtime
 *
 * Provides environment variable and command-line argument access
 * for the sys.env and sys.args stdlib modules.
 */
#include "duxrt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Global argv/argc storage (set at startup by the generated main) ──────── */

static int    g_argc = 0;
static char** g_argv = NULL;

void duxrt_sys_init(int argc, char** argv) {
    g_argc = argc;
    g_argv = argv;
}

/* ── Environment variable access ────────────────────────────────────────── */

DuxStr* duxrt_env_fetch(DuxStr* name) {
    const char* n = duxrt_str_cstr(name);
    if (!n) return duxrt_str_new("", 0);
    const char* val = getenv(n);
    if (!val) return duxrt_str_new("", 0);
    return duxrt_str_new(val, (int64_t)strlen(val));
}

int duxrt_env_has(DuxStr* name) {
    const char* n = duxrt_str_cstr(name);
    if (!n) return 0;
    return getenv(n) != NULL ? 1 : 0;
}

int duxrt_env_store(DuxStr* name, DuxStr* value) {
    const char* n = duxrt_str_cstr(name);
    const char* v = duxrt_str_cstr(value);
    if (!n || !v) return 0;
    return setenv(n, v, 1) == 0 ? 1 : 0;
}

int duxrt_env_remove(DuxStr* name) {
    const char* n = duxrt_str_cstr(name);
    if (!n) return 0;
    return unsetenv(n) == 0 ? 1 : 0;
}

/* ── Command-line arguments ──────────────────────────────────────────────── */

int64_t duxrt_args_count(void) {
    return (int64_t)g_argc;
}

DuxStr* duxrt_args_at(int64_t idx) {
    if (idx < 0 || idx >= g_argc) return duxrt_str_new("", 0);
    return duxrt_str_new(g_argv[idx], (int64_t)strlen(g_argv[idx]));
}

DuxList* duxrt_args_all(void) {
    DuxList* list = duxrt_list_new();
    for (int i = 0; i < g_argc; ++i) {
        DuxStr* s = duxrt_str_new(g_argv[i], (int64_t)strlen(g_argv[i]));
        duxrt_list_push(list, s);
    }
    return list;
}

/* ── Exit ────────────────────────────────────────────────────────────────── */

void duxrt_sys_exit(int64_t code) {
    exit((int)code);
}
