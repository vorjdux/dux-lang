/*
 * duxrt.h — Dux runtime library public C API
 * All functions use C linkage so LLVM IR can call them directly.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Memory ──────────────────────────────────────────────────────────────── */
void* duxrt_alloc(int64_t size);
void  duxrt_free(void* ptr);
void  duxrt_assert_fail(const char* file, int line, const char* msg);

/* ── I/O ─────────────────────────────────────────────────────────────────── */
void  duxrt_println_int(int64_t v);
void  duxrt_println_double(double v);
void  duxrt_print_int(int64_t v);
void  duxrt_print_double(double v);

/* ── String (reference-counted) ─────────────────────────────────────────── */

/*
 * DuxStr: reference-counted string.
 *
 * refcount == -1  →  immortal (string literal globals; never freed)
 * refcount == 0   →  freed (invalid, should not be accessed)
 * refcount >  0   →  live; each variable holding the pointer counts +1
 *
 * Memory: a single malloc(sizeof(DuxStr) + len + 1) block; the char data
 * is embedded immediately after the header via a flexible array member.
 * This gives one allocation, one free, and contiguous header+data layout.
 */
typedef struct DuxStr {
    int32_t  refcount;
    int64_t  len;
    char     data[];   /* flexible array member — data starts right after header */
} DuxStr;

/* Allocate a new DuxStr (refcount=1) copying len bytes from data. */
DuxStr* duxrt_str_new(const char* data, int64_t len);

/* Increment refcount (no-op on immortals). Returns s for convenience. */
DuxStr* duxrt_str_retain(DuxStr* s);

/* Decrement refcount; free data+struct when refcount reaches 0. */
void    duxrt_str_release(DuxStr* s);

/* Return the null-terminated char* inside a DuxStr (NULL-safe). */
const char* duxrt_str_cstr(DuxStr* s);

/* I/O helpers that accept DuxStr* */
void    duxrt_println_str(DuxStr* s);
void    duxrt_print_str(DuxStr* s);
DuxStr* duxrt_readline(void);   /* caller owns the returned DuxStr (refcount=1) */

/* String operations — all return a new DuxStr* (refcount=1, caller owns) */
DuxStr* duxrt_str_concat(DuxStr* a, DuxStr* b);
DuxStr* duxrt_str_from_int(int64_t v);
DuxStr* duxrt_str_from_double(double v);
int64_t duxrt_str_length(DuxStr* s);
DuxStr* duxrt_str_index(DuxStr* s, int64_t i);   /* single char as new DuxStr */
DuxStr* duxrt_str_slice(DuxStr* s, int64_t start, int64_t end);
int     duxrt_str_eq(DuxStr* a, DuxStr* b);

/* Generic len (dispatches to DuxStr.len for strings) */
int64_t duxrt_len(DuxStr* s);

/* ── List (dynamic array of void*) ──────────────────────────────────────── */
typedef struct DuxList {
    int64_t  len;
    int64_t  cap;
    void**   data;
} DuxList;

DuxList* duxrt_list_new(void);
void     duxrt_list_push(DuxList* l, void* val);
void*    duxrt_list_get(DuxList* l, int64_t idx);
void     duxrt_list_set(DuxList* l, int64_t idx, void* val);
int64_t  duxrt_list_len(DuxList* l);
void     duxrt_list_free(DuxList* l);

/* ── Dict (open-addressing hash map, char* keys) ─────────────────────────── */
typedef struct DuxDictEntry {
    char* key;
    void* val;
} DuxDictEntry;

typedef struct DuxDict {
    int64_t       len;
    int64_t       cap;
    DuxDictEntry* entries;
} DuxDict;

DuxDict* duxrt_dict_new(void);
void     duxrt_dict_set(DuxDict* d, const char* key, void* val);
void*    duxrt_dict_get(DuxDict* d, const char* key);
int      duxrt_dict_has(DuxDict* d, const char* key);
void     duxrt_dict_del(DuxDict* d, const char* key);
void     duxrt_dict_free(DuxDict* d);

/* ── Range ───────────────────────────────────────────────────────────────── */
typedef struct DuxRange {
    int64_t start;
    int64_t end;
    int64_t step;
    int     inclusive;
} DuxRange;

DuxRange duxrt_range_excl(int64_t start, int64_t end);
DuxRange duxrt_range_incl(int64_t start, int64_t end);
int64_t  duxrt_range_len(DuxRange r);

/* ── Math ────────────────────────────────────────────────────────────────── */
double  duxrt_math_sqrt(double x);
double  duxrt_math_pow(double x, double y);
double  duxrt_math_floor(double x);
double  duxrt_math_ceil(double x);
double  duxrt_math_abs_d(double x);
int64_t duxrt_math_abs_i(int64_t x);
double  duxrt_math_min_d(double a, double b);
double  duxrt_math_max_d(double a, double b);
int64_t duxrt_math_min_i(int64_t a, int64_t b);
int64_t duxrt_math_max_i(int64_t a, int64_t b);
double  duxrt_math_log(double x);
double  duxrt_math_log2(double x);
double  duxrt_math_sin(double x);
double  duxrt_math_cos(double x);

/* ── Exceptions ──────────────────────────────────────────────────────────── */
typedef struct DuxException {
    const char* type_name;
    char*       message;
    int         line;
    const char* file;
} DuxException;

DuxException* duxrt_exception_new(const char* type, const char* msg);
void          duxrt_exception_free(DuxException* e);

/*
 * try/catch plumbing via setjmp.
 * Usage in generated IR:
 *   %slot = alloca ptr
 *   %in_catch = call i32 @duxrt_try_enter(ptr %slot)
 *   br i1 (icmp ne i32 %in_catch, 0), catch_bb, try_bb
 * try_bb:
 *   ... try body ...
 *   call void @duxrt_try_exit()
 *   br end_bb
 * catch_bb:
 *   ... catch body ...
 *   br end_bb
 */
int  duxrt_try_enter(void** exception_out); /* returns 0 in try, 1 in catch */
void duxrt_try_exit(void);
void duxrt_throw(DuxException* e);          /* longjmp to nearest try frame */

#ifdef __cplusplus
} /* extern "C" */
#endif
