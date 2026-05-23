/*
 * duxrt — JSON runtime
 *
 * A single-file, zero-dependency JSON parser/emitter.
 * The value model uses tagged union:
 *   type 0 = null
 *   type 1 = bool  (i64: 0/1)
 *   type 2 = int   (i64)
 *   type 3 = float (double)
 *   type 4 = str   (DuxStr*)
 *   type 5 = array (DuxList* of DuxJsonVal*)
 *   type 6 = object (DuxList* of DuxJsonKV*)
 *
 * Values are heap-allocated structs; callers use duxrt_json_free() to
 * release them.  duxrt_json_parse() / duxrt_json_stringify() are the main
 * entry points.
 */
#include "duxrt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

/* ── Value type tags ─────────────────────────────────────────────────────── */
#define JSON_NULL   0
#define JSON_BOOL   1
#define JSON_INT    2
#define JSON_FLOAT  3
#define JSON_STR    4
#define JSON_ARRAY  5
#define JSON_OBJECT 6

/* ── Value struct ────────────────────────────────────────────────────────── */

typedef struct DuxJsonKV {
    DuxStr*        key;
    struct DuxJsonVal* val;
} DuxJsonKV;

struct DuxJsonVal {
    int64_t  type;
    union {
        int64_t  b;      /* JSON_BOOL, JSON_INT */
        double   f;      /* JSON_FLOAT */
        DuxStr*  s;      /* JSON_STR */
        DuxList* list;   /* JSON_ARRAY: list of DuxJsonVal* */
                         /* JSON_OBJECT: list of DuxJsonKV* */
    };
};

typedef struct DuxJsonVal DuxJsonVal;

/* ── Allocators ──────────────────────────────────────────────────────────── */

static DuxJsonVal* json_val_new(int type) {
    DuxJsonVal* v = (DuxJsonVal*)duxrt_alloc(sizeof(DuxJsonVal));
    v->type = type;
    v->b    = 0;
    return v;
}

/* ── Free ────────────────────────────────────────────────────────────────── */

void duxrt_json_free(DuxJsonVal* v) {
    if (!v) return;
    if (v->type == JSON_STR && v->s) {
        duxrt_str_release(v->s);
    } else if (v->type == JSON_ARRAY && v->list) {
        for (int64_t i = 0; i < v->list->len; ++i)
            duxrt_json_free((DuxJsonVal*)v->list->data[i]);
        duxrt_list_free(v->list);
    } else if (v->type == JSON_OBJECT && v->list) {
        for (int64_t i = 0; i < v->list->len; ++i) {
            DuxJsonKV* kv = (DuxJsonKV*)v->list->data[i];
            if (kv->key) duxrt_str_release(kv->key);
            duxrt_json_free(kv->val);
            duxrt_free(kv);
        }
        duxrt_list_free(v->list);
    }
    duxrt_free(v);
}

/* ── Parser ──────────────────────────────────────────────────────────────── */

typedef struct { const char* p; const char* end; } Parser;

static void skip_ws(Parser* P) {
    while (P->p < P->end && isspace((unsigned char)*P->p)) ++P->p;
}

static DuxJsonVal* parse_value(Parser* P);

static DuxJsonVal* parse_string_val(Parser* P) {
    if (P->p >= P->end || *P->p != '"') return NULL;
    ++P->p;
    const char* start = P->p;
    /* Find end quote (ignore escapes for now, treat as raw) */
    char buf[65536];
    int64_t len = 0;
    while (P->p < P->end && *P->p != '"') {
        if (*P->p == '\\' && P->p + 1 < P->end) {
            char c = P->p[1];
            ++P->p;
            switch (c) {
                case '"':  buf[len++] = '"';  break;
                case '\\': buf[len++] = '\\'; break;
                case '/':  buf[len++] = '/';  break;
                case 'n':  buf[len++] = '\n'; break;
                case 'r':  buf[len++] = '\r'; break;
                case 't':  buf[len++] = '\t'; break;
                case 'b':  buf[len++] = '\b'; break;
                case 'f':  buf[len++] = '\f'; break;
                default:   buf[len++] = c;    break;
            }
        } else {
            if (len < (int64_t)sizeof(buf) - 1)
                buf[len++] = *P->p;
        }
        ++P->p;
    }
    if (P->p < P->end) ++P->p; /* skip closing " */
    DuxJsonVal* v = json_val_new(JSON_STR);
    v->s = duxrt_str_new(buf, len);
    return v;
}

static DuxJsonVal* parse_number(Parser* P) {
    const char* start = P->p;
    int is_float = 0;
    if (*P->p == '-') ++P->p;
    while (P->p < P->end && isdigit((unsigned char)*P->p)) ++P->p;
    if (P->p < P->end && (*P->p == '.' || *P->p == 'e' || *P->p == 'E')) {
        is_float = 1;
        ++P->p;
        while (P->p < P->end && (isdigit((unsigned char)*P->p) ||
               *P->p == '+' || *P->p == '-' || *P->p == 'e' || *P->p == 'E'))
            ++P->p;
    }
    char tmp[64];
    int64_t slen = P->p - start;
    if (slen >= 63) slen = 63;
    memcpy(tmp, start, (size_t)slen);
    tmp[slen] = '\0';
    if (is_float) {
        DuxJsonVal* v = json_val_new(JSON_FLOAT);
        v->f = strtod(tmp, NULL);
        return v;
    } else {
        DuxJsonVal* v = json_val_new(JSON_INT);
        v->b = (int64_t)strtoll(tmp, NULL, 10);
        return v;
    }
}

static DuxJsonVal* parse_array(Parser* P) {
    ++P->p; /* skip [ */
    DuxJsonVal* v = json_val_new(JSON_ARRAY);
    v->list = duxrt_list_new();
    skip_ws(P);
    if (P->p < P->end && *P->p == ']') { ++P->p; return v; }
    while (P->p < P->end) {
        skip_ws(P);
        DuxJsonVal* elem = parse_value(P);
        if (!elem) break;
        duxrt_list_push(v->list, elem);
        skip_ws(P);
        if (P->p < P->end && *P->p == ',') ++P->p;
        else if (P->p < P->end && *P->p == ']') { ++P->p; break; }
        else break;
    }
    return v;
}

static DuxJsonVal* parse_object(Parser* P) {
    ++P->p; /* skip { */
    DuxJsonVal* v = json_val_new(JSON_OBJECT);
    v->list = duxrt_list_new();
    skip_ws(P);
    if (P->p < P->end && *P->p == '}') { ++P->p; return v; }
    while (P->p < P->end) {
        skip_ws(P);
        if (P->p >= P->end || *P->p != '"') break;
        DuxJsonVal* ks = parse_string_val(P);
        if (!ks) break;
        DuxStr* key = ks->s; ks->s = NULL; duxrt_free(ks);
        skip_ws(P);
        if (P->p < P->end && *P->p == ':') ++P->p;
        skip_ws(P);
        DuxJsonVal* val = parse_value(P);
        DuxJsonKV* kv = (DuxJsonKV*)duxrt_alloc(sizeof(DuxJsonKV));
        kv->key = key;
        kv->val = val;
        duxrt_list_push(v->list, kv);
        skip_ws(P);
        if (P->p < P->end && *P->p == ',') ++P->p;
        else if (P->p < P->end && *P->p == '}') { ++P->p; break; }
        else break;
    }
    return v;
}

static DuxJsonVal* parse_value(Parser* P) {
    skip_ws(P);
    if (P->p >= P->end) return json_val_new(JSON_NULL);
    char c = *P->p;
    if (c == '"') return parse_string_val(P);
    if (c == '[') return parse_array(P);
    if (c == '{') return parse_object(P);
    if (c == 't' && P->p + 4 <= P->end && strncmp(P->p, "true", 4) == 0) {
        P->p += 4;
        DuxJsonVal* v = json_val_new(JSON_BOOL);
        v->b = 1; return v;
    }
    if (c == 'f' && P->p + 5 <= P->end && strncmp(P->p, "false", 5) == 0) {
        P->p += 5;
        DuxJsonVal* v = json_val_new(JSON_BOOL);
        v->b = 0; return v;
    }
    if (c == 'n' && P->p + 4 <= P->end && strncmp(P->p, "null", 4) == 0) {
        P->p += 4;
        return json_val_new(JSON_NULL);
    }
    if (c == '-' || isdigit((unsigned char)c)) return parse_number(P);
    return json_val_new(JSON_NULL);
}

/* ── Public parse ────────────────────────────────────────────────────────── */

DuxJsonVal* duxrt_json_parse(DuxStr* s) {
    const char* cstr = duxrt_str_cstr(s);
    if (!cstr) return json_val_new(JSON_NULL);
    Parser P;
    P.p   = cstr;
    P.end = cstr + s->len;
    return parse_value(&P);
}

/* ── Stringify ───────────────────────────────────────────────────────────── */

static void emit_str_chars(const char* s, int64_t len, char** buf, size_t* used, size_t* cap) {
    for (int64_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)s[i];
        char tmp[8];
        int tl = 1;
        if (ch == '"')       { tmp[0] = '\\'; tmp[1] = '"';  tl = 2; }
        else if (ch == '\\') { tmp[0] = '\\'; tmp[1] = '\\'; tl = 2; }
        else if (ch == '\n') { tmp[0] = '\\'; tmp[1] = 'n';  tl = 2; }
        else if (ch == '\r') { tmp[0] = '\\'; tmp[1] = 'r';  tl = 2; }
        else if (ch == '\t') { tmp[0] = '\\'; tmp[1] = 't';  tl = 2; }
        else                 { tmp[0] = (char)ch; }
        while (*used + (size_t)tl + 1 > *cap) {
            *cap *= 2;
            *buf = (char*)realloc(*buf, *cap);
        }
        memcpy(*buf + *used, tmp, (size_t)tl);
        *used += (size_t)tl;
    }
}

#define EMIT(lit) do { \
    size_t _n = strlen(lit); \
    while (*used + _n + 1 > *cap) { *cap *= 2; *buf = (char*)realloc(*buf, *cap); } \
    memcpy(*buf + *used, lit, _n); *used += _n; \
} while(0)

static void stringify_val(DuxJsonVal* v, char** buf, size_t* used, size_t* cap) {
    if (!v) { EMIT("null"); return; }
    switch (v->type) {
    case JSON_NULL:  EMIT("null"); break;
    case JSON_BOOL:  EMIT(v->b ? "true" : "false"); break;
    case JSON_INT: {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%lld", (long long)v->b);
        while (*used + strlen(tmp) + 1 > *cap) { *cap *= 2; *buf = (char*)realloc(*buf, *cap); }
        size_t tl = strlen(tmp);
        memcpy(*buf + *used, tmp, tl); *used += tl;
        break;
    }
    case JSON_FLOAT: {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%g", v->f);
        while (*used + strlen(tmp) + 1 > *cap) { *cap *= 2; *buf = (char*)realloc(*buf, *cap); }
        size_t tl = strlen(tmp);
        memcpy(*buf + *used, tmp, tl); *used += tl;
        break;
    }
    case JSON_STR: {
        EMIT("\"");
        const char* s = duxrt_str_cstr(v->s);
        emit_str_chars(s, v->s->len, buf, used, cap);
        EMIT("\"");
        break;
    }
    case JSON_ARRAY: {
        EMIT("[");
        for (int64_t i = 0; i < v->list->len; ++i) {
            if (i > 0) EMIT(",");
            stringify_val((DuxJsonVal*)v->list->data[i], buf, used, cap);
        }
        EMIT("]");
        break;
    }
    case JSON_OBJECT: {
        EMIT("{");
        for (int64_t i = 0; i < v->list->len; ++i) {
            if (i > 0) EMIT(",");
            DuxJsonKV* kv = (DuxJsonKV*)v->list->data[i];
            EMIT("\"");
            const char* ks = duxrt_str_cstr(kv->key);
            emit_str_chars(ks, kv->key->len, buf, used, cap);
            EMIT("\":");
            stringify_val(kv->val, buf, used, cap);
        }
        EMIT("}");
        break;
    }
    }
}

DuxStr* duxrt_json_stringify(DuxJsonVal* v) {
    size_t cap = 256, used = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return duxrt_str_new("null", 4);
    stringify_val(v, &buf, &used, &cap);
    buf[used] = '\0';
    DuxStr* result = duxrt_str_new(buf, (int64_t)used);
    free(buf);
    return result;
}

/* ── Accessors ───────────────────────────────────────────────────────────── */

int64_t duxrt_json_type(DuxJsonVal* v) { return v ? v->type : JSON_NULL; }

int64_t duxrt_json_int(DuxJsonVal* v) {
    if (!v) return 0;
    if (v->type == JSON_INT || v->type == JSON_BOOL) return v->b;
    if (v->type == JSON_FLOAT) return (int64_t)v->f;
    return 0;
}

double duxrt_json_float(DuxJsonVal* v) {
    if (!v) return 0.0;
    if (v->type == JSON_FLOAT) return v->f;
    if (v->type == JSON_INT)   return (double)v->b;
    return 0.0;
}

DuxStr* duxrt_json_str(DuxJsonVal* v) {
    if (!v || v->type != JSON_STR || !v->s) return duxrt_str_new("", 0);
    return duxrt_str_retain(v->s);
}

int duxrt_json_bool(DuxJsonVal* v) {
    if (!v) return 0;
    if (v->type == JSON_BOOL || v->type == JSON_INT) return v->b != 0;
    if (v->type == JSON_FLOAT) return v->f != 0.0;
    return 0;
}

int64_t duxrt_json_len(DuxJsonVal* v) {
    if (!v || (v->type != JSON_ARRAY && v->type != JSON_OBJECT)) return 0;
    return v->list->len;
}

DuxJsonVal* duxrt_json_index(DuxJsonVal* v, int64_t idx) {
    if (!v || v->type != JSON_ARRAY || !v->list) return json_val_new(JSON_NULL);
    if (idx < 0 || idx >= v->list->len) return json_val_new(JSON_NULL);
    return (DuxJsonVal*)v->list->data[idx];
}

DuxJsonVal* duxrt_json_field(DuxJsonVal* v, DuxStr* key) {
    if (!v || v->type != JSON_OBJECT || !v->list) return json_val_new(JSON_NULL);
    const char* k = duxrt_str_cstr(key);
    for (int64_t i = 0; i < v->list->len; ++i) {
        DuxJsonKV* kv = (DuxJsonKV*)v->list->data[i];
        if (kv->key && strcmp(duxrt_str_cstr(kv->key), k) == 0)
            return kv->val;
    }
    return json_val_new(JSON_NULL);
}
