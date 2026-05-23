/*
 * duxrt — regular expression runtime
 *
 * Provides POSIX extended regex for the data.regex stdlib module.
 * Uses <regex.h> (POSIX ERE — regex.h is always available on Linux/Mac).
 */
#include "duxrt.h"
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Opaque regex handle ─────────────────────────────────────────────────── */

typedef struct DuxRegex {
    regex_t re;
    int     compiled;
} DuxRegex;

/* ── Compile ─────────────────────────────────────────────────────────────── */

DuxRegex* duxrt_regex_compile(DuxStr* pattern) {
    const char* p = duxrt_str_cstr(pattern);
    if (!p) return NULL;
    DuxRegex* rx = (DuxRegex*)duxrt_alloc(sizeof(DuxRegex));
    rx->compiled = (regcomp(&rx->re, p, REG_EXTENDED | REG_NEWLINE) == 0) ? 1 : 0;
    return rx;
}

/* ── Free ────────────────────────────────────────────────────────────────── */

void duxrt_regex_free(DuxRegex* rx) {
    if (!rx) return;
    if (rx->compiled) regfree(&rx->re);
    duxrt_free(rx);
}

/* ── Test (does the string match?) ──────────────────────────────────────── */

int duxrt_regex_test(DuxRegex* rx, DuxStr* s) {
    if (!rx || !rx->compiled || !s) return 0;
    const char* str = duxrt_str_cstr(s);
    return regexec(&rx->re, str, 0, NULL, 0) == 0 ? 1 : 0;
}

/* ── Find first match → str (empty if no match) ──────────────────────────── */

DuxStr* duxrt_regex_find(DuxRegex* rx, DuxStr* s) {
    if (!rx || !rx->compiled || !s) return duxrt_str_new("", 0);
    const char* str = duxrt_str_cstr(s);
    regmatch_t m;
    if (regexec(&rx->re, str, 1, &m, 0) != 0) return duxrt_str_new("", 0);
    int64_t len = (int64_t)(m.rm_eo - m.rm_so);
    return duxrt_str_new(str + m.rm_so, len);
}

/* ── Find all matches → DuxList of str ───────────────────────────────────── */

DuxList* duxrt_regex_find_all(DuxRegex* rx, DuxStr* s) {
    DuxList* list = duxrt_list_new();
    if (!rx || !rx->compiled || !s) return list;
    const char* str = duxrt_str_cstr(s);
    const char* p = str;
    regmatch_t m;
    while (regexec(&rx->re, p, 1, &m, 0) == 0) {
        int64_t len = (int64_t)(m.rm_eo - m.rm_so);
        DuxStr* match = duxrt_str_new(p + m.rm_so, len);
        duxrt_list_push(list, match);
        p += m.rm_eo;
        if (m.rm_eo == m.rm_so) ++p; /* avoid infinite loop on zero-length match */
    }
    return list;
}

/* ── Replace first match ─────────────────────────────────────────────────── */

DuxStr* duxrt_regex_replace(DuxRegex* rx, DuxStr* s, DuxStr* replacement) {
    if (!rx || !rx->compiled || !s || !replacement) {
        return s ? duxrt_str_retain(s) : duxrt_str_new("", 0);
    }
    const char* str = duxrt_str_cstr(s);
    const char* rep = duxrt_str_cstr(replacement);
    regmatch_t m;
    if (regexec(&rx->re, str, 1, &m, 0) != 0) {
        return duxrt_str_retain(s);
    }
    int64_t before = (int64_t)m.rm_so;
    int64_t replen = (int64_t)strlen(rep);
    int64_t after  = (int64_t)(s->len - m.rm_eo);
    int64_t total  = before + replen + after;
    char* buf = (char*)malloc((size_t)(total + 1));
    if (!buf) return duxrt_str_retain(s);
    memcpy(buf, str, (size_t)before);
    memcpy(buf + before, rep, (size_t)replen);
    memcpy(buf + before + replen, str + m.rm_eo, (size_t)after);
    buf[total] = '\0';
    DuxStr* result = duxrt_str_new(buf, total);
    free(buf);
    return result;
}

/* ── Replace all matches ─────────────────────────────────────────────────── */

DuxStr* duxrt_regex_replace_all(DuxRegex* rx, DuxStr* s, DuxStr* replacement) {
    if (!rx || !rx->compiled || !s || !replacement) {
        return s ? duxrt_str_retain(s) : duxrt_str_new("", 0);
    }
    const char* str = duxrt_str_cstr(s);
    const char* rep = duxrt_str_cstr(replacement);
    int64_t replen  = (int64_t)strlen(rep);

    char* out = (char*)malloc(4096);
    size_t out_cap = 4096, out_len = 0;

    const char* p = str;
    regmatch_t m;
    while (regexec(&rx->re, p, 1, &m, 0) == 0) {
        /* append before */
        size_t before = (size_t)m.rm_so;
        while (out_len + before + (size_t)replen + 1 > out_cap) {
            out_cap *= 2;
            out = (char*)realloc(out, out_cap);
        }
        memcpy(out + out_len, p, before);
        out_len += before;
        memcpy(out + out_len, rep, (size_t)replen);
        out_len += (size_t)replen;
        p += m.rm_eo;
        if (m.rm_eo == m.rm_so) { if (*p) out[out_len++] = *p++; else break; }
    }
    /* append rest */
    size_t rest = strlen(p);
    while (out_len + rest + 1 > out_cap) { out_cap *= 2; out = (char*)realloc(out, out_cap); }
    memcpy(out + out_len, p, rest);
    out_len += rest;
    out[out_len] = '\0';

    DuxStr* result = duxrt_str_new(out, (int64_t)out_len);
    free(out);
    return result;
}

/* ── Convenience: compile + test in one shot ─────────────────────────────── */

int duxrt_regex_match(DuxStr* pattern, DuxStr* s) {
    DuxRegex* rx = duxrt_regex_compile(pattern);
    int r = duxrt_regex_test(rx, s);
    duxrt_regex_free(rx);
    return r;
}
