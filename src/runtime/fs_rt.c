/*
 * duxrt — filesystem operations runtime
 *
 * Provides directory and filesystem functions for the io.fs stdlib module.
 * All string-returning functions return a new DuxStr* (refcount=1).
 */
#include "duxrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

/* ── Create directory ────────────────────────────────────────────────────── */

int duxrt_fs_mkdir(DuxStr* path) {
    const char* p = duxrt_str_cstr(path);
    if (!p) return 0;
    return mkdir(p, 0755) == 0 ? 1 : 0;
}

/* ── Create directory and all parents ───────────────────────────────────── */

int duxrt_fs_mkdir_all(DuxStr* path) {
    const char* p = duxrt_str_cstr(path);
    if (!p) return 0;

    char* tmp = strdup(p);
    if (!tmp) return 0;

    int64_t len = (int64_t)strlen(tmp);
    for (int64_t i = 1; i < len; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0755);  /* ignore errors (may already exist) */
            tmp[i] = '/';
        }
    }
    int ok = mkdir(tmp, 0755) == 0 || errno == EEXIST;
    free(tmp);
    return ok ? 1 : 0;
}

/* ── Remove empty directory ──────────────────────────────────────────────── */

int duxrt_fs_rmdir(DuxStr* path) {
    const char* p = duxrt_str_cstr(path);
    if (!p) return 0;
    return rmdir(p) == 0 ? 1 : 0;
}

/* ── Remove file ─────────────────────────────────────────────────────────── */

int duxrt_fs_remove(DuxStr* path) {
    const char* p = duxrt_str_cstr(path);
    if (!p) return 0;
    return remove(p) == 0 ? 1 : 0;
}

/* ── Rename (move) ───────────────────────────────────────────────────────── */

int duxrt_fs_rename(DuxStr* src, DuxStr* dst) {
    const char* s = duxrt_str_cstr(src);
    const char* d = duxrt_str_cstr(dst);
    if (!s || !d) return 0;
    return rename(s, d) == 0 ? 1 : 0;
}

/* ── List directory entries ──────────────────────────────────────────────── */

DuxList* duxrt_fs_list_dir(DuxStr* path) {
    const char* p = duxrt_str_cstr(path);
    DuxList* list = duxrt_list_new();
    if (!p) return list;

    DIR* dir = opendir(p);
    if (!dir) return list;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;
        size_t len = strlen(entry->d_name);
        DuxStr* name = duxrt_str_new(entry->d_name, (int64_t)len);
        duxrt_list_push(list, name);
    }
    closedir(dir);
    return list;
}

/* ── Get current working directory ──────────────────────────────────────── */

DuxStr* duxrt_fs_cwd(void) {
    char buf[4096];
    if (getcwd(buf, sizeof(buf)) == NULL) return duxrt_str_new(".", 1);
    return duxrt_str_new(buf, (int64_t)strlen(buf));
}

/* ── Change working directory ────────────────────────────────────────────── */

int duxrt_fs_chdir(DuxStr* path) {
    const char* p = duxrt_str_cstr(path);
    if (!p) return 0;
    return chdir(p) == 0 ? 1 : 0;
}
