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
void  duxrt_println_str(const char* s);
void  duxrt_print_int(int64_t v);
void  duxrt_print_double(double v);
void  duxrt_print_str(const char* s);
char* duxrt_readline(void);          /* caller must duxrt_free() result */

/* ── String ──────────────────────────────────────────────────────────────── */
char* duxrt_str_new(const char* data, int64_t len);  /* copies, null-terminates */
char* duxrt_str_concat(const char* a, const char* b);
char* duxrt_str_from_int(int64_t v);
char* duxrt_str_from_double(double v);
int64_t duxrt_str_length(const char* s);
char* duxrt_str_index(const char* s, int64_t i);     /* single char as str */
char* duxrt_str_slice(const char* s, int64_t start, int64_t end);
int   duxrt_str_eq(const char* a, const char* b);

/* ── Generic len (dispatches on type tag – simplified: works on str) ─────── */
int64_t duxrt_len(const void* obj);

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

/* ── Dict (open-addressing hash map, string keys) ────────────────────────── */
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

/* ── String arena (bump-pointer allocator, freed at program exit) ─────── */
void* duxrt_str_arena_alloc(size_t size);
void  duxrt_str_arena_free_all(void);

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
