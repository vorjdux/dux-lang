/*
 * regex_rt.c — POSIX regex runtime for Dux stdlib data.regex
 * Handles are returned as void* to avoid conflicting struct definitions.
 */
#include "duxrt.h"
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Opaque handle wrapping compiled regex_t */
typedef struct {
    regex_t re;
    int     valid;
} DuxRegex;

void* duxrt_regex_compile(DuxStr* pattern) {
    DuxRegex* r = (DuxRegex*)malloc(sizeof(DuxRegex));
    if (!r) return NULL;
    r->valid = 0;
    if (!pattern) return r;
    int flags = REG_EXTENDED | REG_NEWLINE;
    if (regcomp(&r->re, duxrt_str_cstr(pattern), flags) == 0)
        r->valid = 1;
    return (void*)r;
}

int32_t duxrt_regex_matches(void* handle, DuxStr* text) {
    DuxRegex* r = (DuxRegex*)handle;
    if (!r || !r->valid || !text) return 0;
    return regexec(&r->re, duxrt_str_cstr(text), 0, NULL, 0) == 0 ? 1 : 0;
}

/* Return first match as DuxStr, or "" if no match */
DuxStr* duxrt_regex_find(void* handle, DuxStr* text) {
    DuxRegex* r = (DuxRegex*)handle;
    if (!r || !r->valid || !text) return duxrt_str_new("", 0);
    regmatch_t m[1];
    const char* s = duxrt_str_cstr(text);
    if (regexec(&r->re, s, 1, m, 0) != 0) return duxrt_str_new("", 0);
    return duxrt_str_new(s + m[0].rm_so, (int64_t)(m[0].rm_eo - m[0].rm_so));
}

/* Replace first match in text with replacement */
DuxStr* duxrt_regex_replace(void* handle, DuxStr* text, DuxStr* replacement) {
    DuxRegex* r = (DuxRegex*)handle;
    if (!r || !r->valid || !text || !replacement)
        return text ? duxrt_str_retain(text) : duxrt_str_new("", 0);
    regmatch_t m[1];
    const char* s = duxrt_str_cstr(text);
    if (regexec(&r->re, s, 1, m, 0) != 0)
        return duxrt_str_retain(text);
    const char* repl = duxrt_str_cstr(replacement);
    size_t before      = (size_t)m[0].rm_so;
    size_t after_start = (size_t)m[0].rm_eo;
    size_t after       = strlen(s + after_start);
    size_t repl_len    = strlen(repl);
    size_t total       = before + repl_len + after;
    char* buf = (char*)malloc(total + 1);
    if (!buf) return duxrt_str_retain(text);
    memcpy(buf, s, before);
    memcpy(buf + before, repl, repl_len);
    memcpy(buf + before + repl_len, s + after_start, after);
    buf[total] = '\0';
    DuxStr* result = duxrt_str_new(buf, (int64_t)total);
    free(buf);
    return result;
}

/* Find all non-overlapping matches, return as DuxList of DuxStr* */
DuxList* duxrt_regex_find_all(void* handle, DuxStr* text) {
    DuxRegex* r = (DuxRegex*)handle;
    DuxList* list = duxrt_list_new();
    if (!r || !r->valid || !text) return list;
    const char* s = duxrt_str_cstr(text);
    regmatch_t m[1];
    while (*s && regexec(&r->re, s, 1, m, 0) == 0) {
        duxrt_list_push(list,
            duxrt_str_new(s + m[0].rm_so, (int64_t)(m[0].rm_eo - m[0].rm_so)));
        s += m[0].rm_eo;
        if (m[0].rm_eo == m[0].rm_so) s++; /* avoid infinite loop on empty match */
    }
    return list;
}

void duxrt_regex_free(void* handle) {
    DuxRegex* r = (DuxRegex*)handle;
    if (!r) return;
    if (r->valid) regfree(&r->re);
    free(r);
}

/* Compile with flags: 1=IGNORE_CASE, 2=MULTILINE, 4=DOT_ALL (ignored for POSIX) */
void* duxrt_regex_compile_flags(DuxStr* pattern, int32_t flags) {
    DuxRegex* r = (DuxRegex*)malloc(sizeof(DuxRegex));
    if (!r) return NULL;
    r->valid = 0;
    if (!pattern) return r;
    int cflags = REG_EXTENDED | REG_NEWLINE;
    if (flags & 1) cflags |= REG_ICASE;
    if (regcomp(&r->re, duxrt_str_cstr(pattern), cflags) == 0)
        r->valid = 1;
    return (void*)r;
}

/* Replace all non-overlapping matches */
DuxStr* duxrt_regex_replace_all(void* handle, DuxStr* text, DuxStr* replacement) {
    DuxRegex* r = (DuxRegex*)handle;
    if (!r || !r->valid || !text || !replacement)
        return text ? duxrt_str_retain(text) : duxrt_str_new("", 0);
    const char* s    = duxrt_str_cstr(text);
    const char* repl = duxrt_str_cstr(replacement);
    size_t repl_len  = strlen(repl);
    size_t bufsz = strlen(s) * 2 + 1;
    char* buf = (char*)malloc(bufsz);
    if (!buf) return duxrt_str_retain(text);
    size_t pos = 0;
    regmatch_t m[1];
    const char* cur = s;
    while (*cur && regexec(&r->re, cur, 1, m, 0) == 0) {
        if (m[0].rm_eo == m[0].rm_so) { /* empty match — advance one char */
            while (pos + 1 >= bufsz) { bufsz *= 2; buf = (char*)realloc(buf, bufsz); }
            buf[pos++] = *cur++;
            continue;
        }
        size_t before = (size_t)m[0].rm_so;
        size_t needed = pos + before + repl_len + 1;
        while (needed >= bufsz) { bufsz *= 2; buf = (char*)realloc(buf, bufsz); }
        memcpy(buf + pos, cur, before);  pos += before;
        memcpy(buf + pos, repl, repl_len); pos += repl_len;
        cur += m[0].rm_eo;
    }
    /* append remainder */
    size_t tail = strlen(cur);
    while (pos + tail + 1 >= bufsz) { bufsz *= 2; buf = (char*)realloc(buf, bufsz); }
    memcpy(buf + pos, cur, tail); pos += tail;
    buf[pos] = '\0';
    DuxStr* result = duxrt_str_new(buf, (int64_t)pos);
    free(buf);
    return result;
}

/* Return capture groups for the first match as a list of strings.
 * Index 0 = full match, 1.. = groups. Returns empty list if no match. */
DuxList* duxrt_regex_captures(void* handle, DuxStr* text) {
    DuxRegex* r = (DuxRegex*)handle;
    DuxList* list = duxrt_list_new();
    if (!r || !r->valid || !text) return list;
    regmatch_t m[33];
    const char* s = duxrt_str_cstr(text);
    if (regexec(&r->re, s, 33, m, 0) != 0) return list;
    for (int i = 0; i < 33; i++) {
        if (m[i].rm_so < 0) break;
        duxrt_list_push(list,
            duxrt_str_new(s + m[i].rm_so, (int64_t)(m[i].rm_eo - m[i].rm_so)));
    }
    return list;
}

/* Split text around all matches; returns list of substrings between matches */
DuxList* duxrt_regex_split(void* handle, DuxStr* text) {
    DuxRegex* r = (DuxRegex*)handle;
    DuxList* list = duxrt_list_new();
    if (!r || !r->valid || !text) {
        if (text) duxrt_list_push(list, duxrt_str_retain(text));
        return list;
    }
    const char* s    = duxrt_str_cstr(text);
    const char* prev = s;
    regmatch_t m[1];
    while (*s && regexec(&r->re, s, 1, m, 0) == 0) {
        if (m[0].rm_eo == m[0].rm_so) { s++; continue; }
        duxrt_list_push(list, duxrt_str_new(prev, (int64_t)(s + m[0].rm_so - prev)));
        prev = s + m[0].rm_eo;
        s    = prev;
    }
    duxrt_list_push(list, duxrt_str_new(prev, (int64_t)strlen(prev)));
    return list;
}

/* Test if compiled pattern is valid */
int32_t duxrt_regex_is_valid(void* handle) {
    DuxRegex* r = (DuxRegex*)handle;
    return (r && r->valid) ? 1 : 0;
}
