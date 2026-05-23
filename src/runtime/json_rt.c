/*
 * json_rt.c — minimal JSON runtime for Dux stdlib data.json
 *
 * Provides:
 *   duxrt_json_parse(DuxStr*)       → void* (DuxDict*) or non-NULL empty dict
 *   duxrt_json_stringify(void*)     → DuxStr*
 *   duxrt_json_get_str(void*, key)  → DuxStr*
 *   duxrt_json_get_int(void*, key)  → int64_t
 *   duxrt_json_has(void*, key)      → int32_t
 *
 * Opaque handles use void* to avoid conflicting struct definitions.
 * Only flat string/number/bool object values supported; sufficient for basic use.
 */
#include "duxrt.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static const char* skip_ws(const char* p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char* parse_string(const char* p, char* buf, size_t bufsz) {
    if (!p || *p != '"') return NULL;
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"':  if (i < bufsz-1) buf[i++] = '"';  break;
                case '\\': if (i < bufsz-1) buf[i++] = '\\'; break;
                case 'n':  if (i < bufsz-1) buf[i++] = '\n'; break;
                case 't':  if (i < bufsz-1) buf[i++] = '\t'; break;
                case 'r':  if (i < bufsz-1) buf[i++] = '\r'; break;
                default:   if (i < bufsz-1) buf[i++] = *p;   break;
            }
        } else {
            if (i < bufsz-1) buf[i++] = *p;
        }
        p++;
    }
    buf[i] = '\0';
    if (*p == '"') p++;
    return p;
}

/* Parse a flat JSON object into a DuxDict (returned as void*).
 * Values are stored as DuxStr* (numbers converted to string). */
void* duxrt_json_parse(DuxStr* input) {
    DuxDict* d = duxrt_dict_new();
    if (!input) return d;
    const char* p = duxrt_str_cstr(input);
    if (!p) return d;
    p = skip_ws(p);
    if (*p != '{') return d;
    p++;
    p = skip_ws(p);
    if (*p == '}') return d;

    char key[512];
    char val[4096];

    while (*p) {
        p = skip_ws(p);
        if (*p == '}') break;
        if (*p == ',') { p++; continue; }

        /* Parse key */
        const char* next = parse_string(p, key, sizeof(key));
        if (!next) break;
        p = skip_ws(next);
        if (*p != ':') break;
        p++;
        p = skip_ws(p);

        /* Parse value */
        DuxStr* value_str = NULL;
        if (*p == '"') {
            next = parse_string(p, val, sizeof(val));
            if (!next) break;
            value_str = duxrt_str_new(val, (int64_t)strlen(val));
            p = next;
        } else if (*p == 't' && strncmp(p, "true", 4) == 0) {
            value_str = duxrt_str_new("true", 4);
            p += 4;
        } else if (*p == 'f' && strncmp(p, "false", 5) == 0) {
            value_str = duxrt_str_new("false", 5);
            p += 5;
        } else if (*p == 'n' && strncmp(p, "null", 4) == 0) {
            value_str = duxrt_str_new("null", 4);
            p += 4;
        } else {
            /* number */
            const char* start = p;
            if (*p == '-') p++;
            while (*p && (isdigit((unsigned char)*p) || *p == '.' ||
                          *p == 'e' || *p == 'E' || *p == '+' || *p == '-'))
                p++;
            size_t n = (size_t)(p - start);
            if (n >= sizeof(val)) n = sizeof(val) - 1;
            memcpy(val, start, n);
            val[n] = '\0';
            value_str = duxrt_str_new(val, (int64_t)n);
        }

        if (value_str)
            duxrt_dict_set(d, key, value_str);
    }
    return (void*)d;
}

DuxStr* duxrt_json_get_str(void* handle, DuxStr* key) {
    DuxDict* d = (DuxDict*)handle;
    if (!d || !key) return duxrt_str_new("", 0);
    void* v = duxrt_dict_get(d, duxrt_str_cstr(key));
    if (!v) return duxrt_str_new("", 0);
    return duxrt_str_retain((DuxStr*)v);
}

int64_t duxrt_json_get_int(void* handle, DuxStr* key) {
    DuxDict* d = (DuxDict*)handle;
    if (!d || !key) return 0;
    void* v = duxrt_dict_get(d, duxrt_str_cstr(key));
    if (!v) return 0;
    return (int64_t)atoll(duxrt_str_cstr((DuxStr*)v));
}

double duxrt_json_get_double(void* handle, DuxStr* key) {
    DuxDict* d = (DuxDict*)handle;
    if (!d || !key) return 0.0;
    void* v = duxrt_dict_get(d, duxrt_str_cstr(key));
    if (!v) return 0.0;
    return atof(duxrt_str_cstr((DuxStr*)v));
}

int32_t duxrt_json_has(void* handle, DuxStr* key) {
    DuxDict* d = (DuxDict*)handle;
    if (!d || !key) return 0;
    return duxrt_dict_has(d, duxrt_str_cstr(key)) ? 1 : 0;
}

/* Serialize a flat DuxDict to a JSON object string */
DuxStr* duxrt_json_stringify(void* handle) {
    DuxDict* d = (DuxDict*)handle;
    if (!d) return duxrt_str_new("{}", 2);
    size_t bufsz = 256;
    char* buf = (char*)malloc(bufsz);
    if (!buf) return duxrt_str_new("{}", 2);
    size_t pos = 0;
    buf[pos++] = '{';
    int first = 1;
    for (int64_t i = 0; i < d->cap; i++) {
        if (!d->entries[i].key || d->entries[i].key == (char*)(uintptr_t)1) continue;
        const char* k = d->entries[i].key;
        DuxStr* vs = (DuxStr*)d->entries[i].val;
        const char* v = vs ? duxrt_str_cstr(vs) : "";
        size_t klen = strlen(k);
        size_t vlen = strlen(v);
        size_t needed = 1 + klen + 4 + vlen + 2;
        while (pos + needed + 2 > bufsz) {
            bufsz *= 2;
            char* nbuf = (char*)realloc(buf, bufsz);
            if (!nbuf) { free(buf); return duxrt_str_new("{}", 2); }
            buf = nbuf;
        }
        if (!first) buf[pos++] = ',';
        first = 0;
        buf[pos++] = '"';
        memcpy(buf + pos, k, klen); pos += klen;
        buf[pos++] = '"';
        buf[pos++] = ':';
        buf[pos++] = '"';
        memcpy(buf + pos, v, vlen); pos += vlen;
        buf[pos++] = '"';
    }
    buf[pos++] = '}';
    buf[pos]   = '\0';
    DuxStr* result = duxrt_str_new(buf, (int64_t)pos);
    free(buf);
    return result;
}
