/*
 * fs_rt.c — filesystem operations runtime for Dux stdlib io.fs
 */
#include "duxrt.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

/* Create intermediate directories along a path */
static int mkdir_p(const char* path) {
    char tmp[4096];
    char* p;
    size_t len;
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len == 0) return -1;
    if (tmp[len - 1] == '/') tmp[--len] = '\0';
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    return mkdir(tmp, 0777);
}

int32_t duxrt_fs_mkdir(DuxStr* path) {
    if (!path) return -1;
    return mkdir(duxrt_str_cstr(path), 0777) == 0 ? 0 : -1;
}

int32_t duxrt_fs_mkdir_all(DuxStr* path) {
    if (!path) return -1;
    int r = mkdir_p(duxrt_str_cstr(path));
    return (r == 0 || errno == EEXIST) ? 0 : -1;
}

int32_t duxrt_fs_rmdir(DuxStr* path) {
    if (!path) return -1;
    return rmdir(duxrt_str_cstr(path)) == 0 ? 0 : -1;
}

int32_t duxrt_fs_remove(DuxStr* path) {
    if (!path) return -1;
    return remove(duxrt_str_cstr(path)) == 0 ? 0 : -1;
}

int32_t duxrt_fs_rename(DuxStr* from, DuxStr* to) {
    if (!from || !to) return -1;
    return rename(duxrt_str_cstr(from), duxrt_str_cstr(to)) == 0 ? 0 : -1;
}

DuxList* duxrt_fs_list_dir(DuxStr* path) {
    DuxList* list = duxrt_list_new();
    if (!path) return list;
    DIR* dir = opendir(duxrt_str_cstr(path));
    if (!dir) return list;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        DuxStr* s = duxrt_str_new(ent->d_name, (int64_t)strlen(ent->d_name));
        duxrt_list_push(list, s);
    }
    closedir(dir);
    return list;
}

DuxStr* duxrt_fs_cwd(void) {
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) return duxrt_str_new("", 0);
    return duxrt_str_new(buf, (int64_t)strlen(buf));
}

int32_t duxrt_fs_chdir(DuxStr* path) {
    if (!path) return -1;
    return chdir(duxrt_str_cstr(path)) == 0 ? 0 : -1;
}

int32_t duxrt_fs_exists(DuxStr* path) {
    if (!path) return 0;
    struct stat st;
    return stat(duxrt_str_cstr(path), &st) == 0 ? 1 : 0;
}

int32_t duxrt_fs_is_dir(DuxStr* path) {
    if (!path) return 0;
    struct stat st;
    if (stat(duxrt_str_cstr(path), &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

int32_t duxrt_fs_is_file(DuxStr* path) {
    if (!path) return 0;
    struct stat st;
    if (stat(duxrt_str_cstr(path), &st) != 0) return 0;
    return S_ISREG(st.st_mode) ? 1 : 0;
}

int64_t duxrt_fs_size(DuxStr* path) {
    if (!path) return -1;
    struct stat st;
    if (stat(duxrt_str_cstr(path), &st) != 0) return -1;
    return (int64_t)st.st_size;
}

int32_t duxrt_fs_copy(DuxStr* src, DuxStr* dst) {
    if (!src || !dst) return -1;
    FILE* in = fopen(duxrt_str_cstr(src), "rb");
    if (!in) return -1;
    FILE* out = fopen(duxrt_str_cstr(dst), "wb");
    if (!out) { fclose(in); return -1; }
    char buf[65536];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    }
    fclose(in);
    fclose(out);
    return ok ? 0 : -1;
}
