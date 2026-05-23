/*
 * duxrt — path operations runtime
 *
 * Provides path manipulation functions for the io.path stdlib module.
 * All functions that return strings return a new DuxStr* (refcount=1).
 */
#include "duxrt.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

/* ── Path join ───────────────────────────────────────────────────────────── */

DuxStr* duxrt_path_join(DuxStr* dir, DuxStr* name) {
    const char* d = duxrt_str_cstr(dir);
    const char* n = duxrt_str_cstr(name);
    if (!d || d[0] == '\0') return duxrt_str_new(n, (int64_t)strlen(n));
    if (!n || n[0] == '\0') return duxrt_str_new(d, (int64_t)strlen(d));

    int64_t dlen = (int64_t)strlen(d);
    int64_t nlen = (int64_t)strlen(n);
    int need_sep = (d[dlen - 1] != '/') ? 1 : 0;
    int64_t total = dlen + need_sep + nlen;

    char* buf = (char*)malloc((size_t)(total + 1));
    if (!buf) return duxrt_str_new("", 0);
    memcpy(buf, d, (size_t)dlen);
    if (need_sep) buf[dlen] = '/';
    memcpy(buf + dlen + need_sep, n, (size_t)nlen);
    buf[total] = '\0';
    DuxStr* result = duxrt_str_new(buf, total);
    free(buf);
    return result;
}

/* ── Basename ────────────────────────────────────────────────────────────── */

DuxStr* duxrt_path_basename(DuxStr* p) {
    const char* s = duxrt_str_cstr(p);
    if (!s || s[0] == '\0') return duxrt_str_new(".", 1);
    int64_t len = (int64_t)strlen(s);
    /* skip trailing slashes */
    int64_t end = len - 1;
    while (end > 0 && s[end] == '/') --end;
    /* find last slash before end */
    int64_t start = end;
    while (start > 0 && s[start - 1] != '/') --start;
    int64_t blen = end - start + 1;
    return duxrt_str_new(s + start, blen);
}

/* ── Dirname ─────────────────────────────────────────────────────────────── */

DuxStr* duxrt_path_dirname(DuxStr* p) {
    const char* s = duxrt_str_cstr(p);
    if (!s || s[0] == '\0') return duxrt_str_new(".", 1);
    int64_t len = (int64_t)strlen(s);
    int64_t end = len - 1;
    /* skip trailing slashes */
    while (end > 0 && s[end] == '/') --end;
    /* walk back to last slash */
    while (end > 0 && s[end] != '/') --end;
    if (end == 0) {
        return s[0] == '/' ? duxrt_str_new("/", 1) : duxrt_str_new(".", 1);
    }
    /* trim trailing slashes from dirname */
    while (end > 1 && s[end - 1] == '/') --end;
    return duxrt_str_new(s, end);
}

/* ── Extension ───────────────────────────────────────────────────────────── */

DuxStr* duxrt_path_extension(DuxStr* p) {
    const char* s = duxrt_str_cstr(p);
    if (!s) return duxrt_str_new("", 0);
    int64_t len = (int64_t)strlen(s);
    /* find last dot after last slash */
    int64_t i = len - 1;
    while (i >= 0 && s[i] != '.' && s[i] != '/') --i;
    if (i < 0 || s[i] != '.') return duxrt_str_new("", 0);
    return duxrt_str_new(s + i, len - i);
}

/* ── Stem (filename without extension) ──────────────────────────────────── */

DuxStr* duxrt_path_stem(DuxStr* p) {
    DuxStr* base = duxrt_path_basename(p);
    const char* s = duxrt_str_cstr(base);
    int64_t len = (int64_t)strlen(s);
    /* find last dot */
    int64_t dot = len - 1;
    while (dot > 0 && s[dot] != '.') --dot;
    DuxStr* result;
    if (dot > 0)
        result = duxrt_str_new(s, dot);
    else
        result = duxrt_str_new(s, len);
    duxrt_str_release(base);
    return result;
}

/* ── Existence checks ────────────────────────────────────────────────────── */

int duxrt_path_exists(DuxStr* p) {
    const char* s = duxrt_str_cstr(p);
    if (!s) return 0;
    struct stat st;
    return stat(s, &st) == 0 ? 1 : 0;
}

int duxrt_path_is_file(DuxStr* p) {
    const char* s = duxrt_str_cstr(p);
    if (!s) return 0;
    struct stat st;
    if (stat(s, &st) != 0) return 0;
    return S_ISREG(st.st_mode) ? 1 : 0;
}

int duxrt_path_is_dir(DuxStr* p) {
    const char* s = duxrt_str_cstr(p);
    if (!s) return 0;
    struct stat st;
    if (stat(s, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

/* ── Absolute path ───────────────────────────────────────────────────────── */

DuxStr* duxrt_path_absolute(DuxStr* p) {
    const char* s = duxrt_str_cstr(p);
    if (!s) return duxrt_str_new("", 0);
    char resolved[PATH_MAX];
    if (realpath(s, resolved) == NULL) {
        /* fallback: return the input unchanged */
        return duxrt_str_new(s, (int64_t)strlen(s));
    }
    return duxrt_str_new(resolved, (int64_t)strlen(resolved));
}
