/*
 * duxrt — file I/O runtime
 *
 * Provides file read/write functions for the io.file stdlib module.
 * All string-returning functions return a new DuxStr* (refcount=1).
 */
#include "duxrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Read entire file into a string ─────────────────────────────────────── */

DuxStr* duxrt_file_read(DuxStr* path) {
    const char* p = duxrt_str_cstr(path);
    if (!p) return duxrt_str_new("", 0);

    FILE* f = fopen(p, "rb");
    if (!f) return duxrt_str_new("", 0);

    /* Determine file size */
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return duxrt_str_new("", 0); }
    long sz = ftell(f);
    if (sz < 0)  { fclose(f); return duxrt_str_new("", 0); }
    rewind(f);

    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return duxrt_str_new("", 0); }

    size_t read = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[read] = '\0';

    DuxStr* result = duxrt_str_new(buf, (int64_t)read);
    free(buf);
    return result;
}

/* ── Read file as lines ──────────────────────────────────────────────────── */

DuxList* duxrt_file_read_lines(DuxStr* path) {
    const char* p = duxrt_str_cstr(path);
    DuxList* list = duxrt_list_new();
    if (!p) return list;

    FILE* f = fopen(p, "r");
    if (!f) return list;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') { line[--len] = '\0'; }
        if (len > 0 && line[len - 1] == '\r') { line[--len] = '\0'; }
        DuxStr* s = duxrt_str_new(line, (int64_t)len);
        duxrt_list_push(list, s);
    }
    fclose(f);
    return list;
}

/* ── Write string to file (overwrite) ───────────────────────────────────── */

int duxrt_file_write(DuxStr* path, DuxStr* content) {
    const char* p = duxrt_str_cstr(path);
    const char* c = duxrt_str_cstr(content);
    if (!p || !c) return 0;

    FILE* f = fopen(p, "wb");
    if (!f) return 0;

    int64_t len = content ? (int64_t)content->len : 0;
    size_t written = fwrite(c, 1, (size_t)len, f);
    fclose(f);
    return (written == (size_t)len) ? 1 : 0;
}

/* ── Append string to file ───────────────────────────────────────────────── */

int duxrt_file_append(DuxStr* path, DuxStr* content) {
    const char* p = duxrt_str_cstr(path);
    const char* c = duxrt_str_cstr(content);
    if (!p || !c) return 0;

    FILE* f = fopen(p, "ab");
    if (!f) return 0;

    int64_t len = content ? (int64_t)content->len : 0;
    size_t written = fwrite(c, 1, (size_t)len, f);
    fclose(f);
    return (written == (size_t)len) ? 1 : 0;
}

/* ── Check if file exists ────────────────────────────────────────────────── */

int duxrt_file_exists(DuxStr* path) {
    const char* p = duxrt_str_cstr(path);
    if (!p) return 0;
    FILE* f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* ── Remove (delete) file ────────────────────────────────────────────────── */

int duxrt_file_remove(DuxStr* path) {
    const char* p = duxrt_str_cstr(path);
    if (!p) return 0;
    return remove(p) == 0 ? 1 : 0;
}

/* ── Copy file ───────────────────────────────────────────────────────────── */

int duxrt_file_copy(DuxStr* src, DuxStr* dst) {
    const char* s = duxrt_str_cstr(src);
    const char* d = duxrt_str_cstr(dst);
    if (!s || !d) return 0;

    FILE* fin  = fopen(s, "rb");
    if (!fin)  return 0;
    FILE* fout = fopen(d, "wb");
    if (!fout) { fclose(fin); return 0; }

    char buf[65536];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
        if (fwrite(buf, 1, n, fout) != n) { ok = 0; break; }
    }
    fclose(fin);
    fclose(fout);
    return ok;
}
