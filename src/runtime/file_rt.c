/*
 * file_rt.c — file I/O runtime for Dux stdlib io.file
 */
#include "duxrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DuxStr* duxrt_file_read(DuxStr* path) {
    if (!path) return duxrt_str_new("", 0);
    FILE* f = fopen(duxrt_str_cstr(path), "rb");
    if (!f) return duxrt_str_new("", 0);
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return duxrt_str_new("", 0); }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return duxrt_str_new("", 0); }
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)(sz + 1));
    if (!buf) { fclose(f); return duxrt_str_new("", 0); }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    DuxStr* s = duxrt_str_new(buf, (int64_t)n);
    free(buf);
    return s;
}

DuxList* duxrt_file_read_lines(DuxStr* path) {
    DuxList* list = duxrt_list_new();
    if (!path) return list;
    FILE* f = fopen(duxrt_str_cstr(path), "r");
    if (!f) return list;
    char* line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, f)) != -1) {
        /* Strip trailing newline */
        if (n > 0 && line[n - 1] == '\n') { line[n - 1] = '\0'; n--; }
        duxrt_list_push(list, duxrt_str_new(line, (int64_t)n));
    }
    free(line);
    fclose(f);
    return list;
}

int32_t duxrt_file_write(DuxStr* path, DuxStr* content) {
    if (!path || !content) return -1;
    FILE* f = fopen(duxrt_str_cstr(path), "w");
    if (!f) return -1;
    const char* s = duxrt_str_cstr(content);
    size_t n = (size_t)content->len;
    size_t written = fwrite(s, 1, n, f);
    fclose(f);
    return (written == n) ? 0 : -1;
}

int32_t duxrt_file_append(DuxStr* path, DuxStr* content) {
    if (!path || !content) return -1;
    FILE* f = fopen(duxrt_str_cstr(path), "a");
    if (!f) return -1;
    const char* s = duxrt_str_cstr(content);
    size_t n = (size_t)content->len;
    size_t written = fwrite(s, 1, n, f);
    fclose(f);
    return (written == n) ? 0 : -1;
}

int32_t duxrt_file_exists(DuxStr* path) {
    if (!path) return 0;
    FILE* f = fopen(duxrt_str_cstr(path), "r");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int32_t duxrt_file_remove(DuxStr* path) {
    if (!path) return -1;
    return remove(duxrt_str_cstr(path)) == 0 ? 0 : -1;
}

int32_t duxrt_file_copy(DuxStr* src, DuxStr* dst) {
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

int64_t duxrt_file_size(DuxStr* path) {
    if (!path) return -1;
    FILE* f = fopen(duxrt_str_cstr(path), "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    fclose(f);
    return (int64_t)sz;
}
