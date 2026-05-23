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
