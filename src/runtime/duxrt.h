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
 * Hybrid allocation strategy:
 *   short strings (len <= DUXSTR_INLINE_MAX):
 *       single malloc(sizeof(DuxStr) + len + 1)  — ext is NULL, data in FAM
 *   long strings  (len >  DUXSTR_INLINE_MAX):
 *       malloc(sizeof(DuxStr)) for the header    — ext points to heap buffer
 *
 * sizeof(DuxStr) = 16 bytes on LP64:
 *   offset  0: int32_t refcount
 *   offset  4: int32_t len      (max 2 GiB; int32_t avoids padding after refcount)
 *   offset  8: char*   ext      (NULL = inline)
 *   offset 16: char    data[]   (FAM — inline data for short strings)
 *
 * Using int32_t for len instead of int64_t eliminates 4 bytes of alignment
 * padding, shrinking the header from 24 to 16 bytes.  Short strings then
 * allocate malloc(16+len+1), landing in the same allocator size-class as a
 * pure-FAM struct — eliminating the bin-boundary penalty seen with 24-byte
 * headers.
 *
 * LLVM literal globals use { i32, i32, ptr, [N+1 x i8] } which produces the
 * same offsets, so immortal literals always have ext=null and the string bytes
 * at offset 16 — duxrt_str_cstr() returns s->data for them correctly.
 */
#define DUXSTR_INLINE_MAX  63   /* strings <= 63 bytes are stored inline (FAM) */

typedef struct DuxStr {
    int32_t  refcount;
    int32_t  len;    /* int32_t: no padding after refcount → header = 16 bytes */
    char*    ext;    /* NULL  → data inline in FAM below (short strings)
                        non-NULL → separate heap buffer    (long strings)  */
    char     data[]; /* inline storage — only valid when ext == NULL */
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

/* ── System environment + args (sys.env / sys.args modules) ─────────────── */
void     duxrt_sys_init(int argc, char** argv);
DuxStr*  duxrt_env_fetch(DuxStr* name);
int      duxrt_env_has(DuxStr* name);
int      duxrt_env_store(DuxStr* name, DuxStr* value);
int      duxrt_env_remove(DuxStr* name);
int64_t  duxrt_args_count(void);
DuxStr*  duxrt_args_at(int64_t idx);
DuxList* duxrt_args_all(void);
void     duxrt_sys_exit(int64_t code);

/* ── Time/clock (time.clock + time.date modules) ────────────────────────── */
int64_t duxrt_clock_now_ns(void);
double  duxrt_clock_now(void);
void    duxrt_clock_sleep_ms(int64_t ms);
int64_t duxrt_clock_unix(void);
DuxStr* duxrt_clock_now_str(void);

int64_t duxrt_date_year(int64_t unix_ts);
int64_t duxrt_date_month(int64_t unix_ts);
int64_t duxrt_date_day(int64_t unix_ts);
int64_t duxrt_date_hour(int64_t unix_ts);
int64_t duxrt_date_minute(int64_t unix_ts);
int64_t duxrt_date_second(int64_t unix_ts);
DuxStr* duxrt_date_format(int64_t unix_ts, DuxStr* fmt);

/* ── Filesystem operations (io.fs module) ───────────────────────────────── */
int      duxrt_fs_mkdir(DuxStr* path);
int      duxrt_fs_mkdir_all(DuxStr* path);
int      duxrt_fs_rmdir(DuxStr* path);
int      duxrt_fs_remove(DuxStr* path);
int      duxrt_fs_rename(DuxStr* src, DuxStr* dst);
DuxList* duxrt_fs_list_dir(DuxStr* path);
DuxStr*  duxrt_fs_cwd(void);
int      duxrt_fs_chdir(DuxStr* path);

/* ── File I/O (io.file module) ──────────────────────────────────────────── */
DuxStr*  duxrt_file_read(DuxStr* path);
DuxList* duxrt_file_read_lines(DuxStr* path);
int      duxrt_file_write(DuxStr* path, DuxStr* content);
int      duxrt_file_append(DuxStr* path, DuxStr* content);
int      duxrt_file_exists(DuxStr* path);
int      duxrt_file_remove(DuxStr* path);
int      duxrt_file_copy(DuxStr* src, DuxStr* dst);

/* ── Path operations (io.path module) ───────────────────────────────────── */
DuxStr* duxrt_path_join(DuxStr* dir, DuxStr* name);
DuxStr* duxrt_path_basename(DuxStr* p);
DuxStr* duxrt_path_dirname(DuxStr* p);
DuxStr* duxrt_path_extension(DuxStr* p);
DuxStr* duxrt_path_stem(DuxStr* p);
int     duxrt_path_exists(DuxStr* p);
int     duxrt_path_is_file(DuxStr* p);
int     duxrt_path_is_dir(DuxStr* p);
DuxStr* duxrt_path_absolute(DuxStr* p);

#ifdef __cplusplus
} /* extern "C" */
#endif
