/*
 * duxrt.h — Dux runtime library public C API
 * All functions use C linkage so LLVM IR can call them directly.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* _Atomic is C11; in C++ mode use a compatibility shim so this header
 * can be included from both C and C++ translation units.                */
#ifdef __cplusplus
#  include <atomic>
#  define DUXRT_ATOMIC(T) std::atomic<T>
#else
#  include <stdatomic.h>
#  define DUXRT_ATOMIC(T) _Atomic T
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration needed by duxrt_str_join_list below. */
typedef struct DuxList DuxList;

/* ── Memory ──────────────────────────────────────────────────────────────── */
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
    DUXRT_ATOMIC(int32_t)  refcount;
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

/* Efficiently joins all DuxStr* elements of a DuxList into one new DuxStr*.
   Returns a new string with refcount=1 (caller owns). O(n) in total length. */
DuxStr* duxrt_str_join_list(DuxList* parts, int64_t count);

/* Generic len (dispatches to DuxStr.len for strings) */
int64_t duxrt_len(DuxStr* s);

/* ── StringBuffer — pre-allocated growing char buffer ─────────────────────
 * Equivalent to std::string with reserve(): amortised O(1) append,
 * O(n) build.  Use duxrt_strbuf_append_str to accumulate parts, then
 * duxrt_strbuf_build to materialise a single DuxStr* at the end.       */
typedef struct DuxStrBuf {
    char*   data;
    int64_t len;
    int64_t cap;
} DuxStrBuf;

DuxStrBuf* duxrt_strbuf_new(void);
void       duxrt_strbuf_append_str(DuxStrBuf* b, DuxStr* s);
DuxStr*    duxrt_strbuf_build(DuxStrBuf* b);   /* refcount=1, caller owns */
void       duxrt_strbuf_free(DuxStrBuf* b);

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
DuxList* duxrt_list_concat(DuxList* a, DuxList* b);

/* ── Dict (open-addressing hash map, char* keys) ─────────────────────────── */
typedef struct DuxDictEntry {
    char* key;
    void* val;
} DuxDictEntry;

typedef struct DuxDict {
    int64_t       len;
    int64_t       cap;
    int64_t       tomb;
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

/* ── Path ────────────────────────────────────────────────────────────────── */
DuxStr*  duxrt_fs_realpath(DuxStr* path);
DuxStr*  duxrt_fs_getcwd(void);

/* ── Filesystem ──────────────────────────────────────────────────────────── */
int32_t  duxrt_fs_mkdir(DuxStr* path);
int32_t  duxrt_fs_mkdir_all(DuxStr* path);
int32_t  duxrt_fs_rmdir(DuxStr* path);
int32_t  duxrt_fs_remove(DuxStr* path);
int32_t  duxrt_fs_rename(DuxStr* from, DuxStr* to);
DuxList* duxrt_fs_list_dir(DuxStr* path);
DuxStr*  duxrt_fs_cwd(void);
int32_t  duxrt_fs_chdir(DuxStr* path);
int32_t  duxrt_fs_exists(DuxStr* path);
int32_t  duxrt_fs_is_dir(DuxStr* path);
int32_t  duxrt_fs_is_file(DuxStr* path);
int64_t  duxrt_fs_size(DuxStr* path);
int32_t  duxrt_fs_copy(DuxStr* src, DuxStr* dst);

/* ── File I/O ────────────────────────────────────────────────────────────── */
DuxStr*  duxrt_file_read(DuxStr* path);
DuxList* duxrt_file_read_lines(DuxStr* path);
int32_t  duxrt_file_write(DuxStr* path, DuxStr* content);
int32_t  duxrt_file_append(DuxStr* path, DuxStr* content);
int32_t  duxrt_file_exists(DuxStr* path);
int32_t  duxrt_file_remove(DuxStr* path);
int32_t  duxrt_file_copy(DuxStr* src, DuxStr* dst);
int64_t  duxrt_file_size(DuxStr* path);

/* ── Time / Clock ────────────────────────────────────────────────────────── */
int64_t  duxrt_clock_now_ns(void);
double   duxrt_clock_now(void);
void     duxrt_clock_sleep_ms(int64_t ms);
int64_t  duxrt_clock_unix(void);
DuxStr*  duxrt_clock_now_str(void);
int64_t  duxrt_date_year(int64_t unix_ts);
int64_t  duxrt_date_month(int64_t unix_ts);
int64_t  duxrt_date_day(int64_t unix_ts);
int64_t  duxrt_date_hour(int64_t unix_ts);
int64_t  duxrt_date_minute(int64_t unix_ts);
int64_t  duxrt_date_second(int64_t unix_ts);
DuxStr*  duxrt_date_format(int64_t unix_ts, DuxStr* fmt);
DuxStr*  duxrt_date_iso(int64_t unix_ts);

/* ── System / Environment / Args ─────────────────────────────────────────── */
void     duxrt_sys_init(int argc, char** argv);
void     duxrt_sys_exit(int64_t code);
DuxStr*  duxrt_env_fetch(DuxStr* name);
int32_t  duxrt_env_has(DuxStr* name);
int32_t  duxrt_env_store(DuxStr* name, DuxStr* val);
int32_t  duxrt_env_remove(DuxStr* name);
int64_t  duxrt_args_count(void);
DuxStr*  duxrt_args_at(int64_t i);
DuxList* duxrt_args_all(void);
int64_t  duxrt_sys_getpid(void);
int64_t  duxrt_sys_getppid(void);
DuxStr*  duxrt_sys_hostname(void);

/* ── Random ──────────────────────────────────────────────────────────────── */
void     duxrt_random_seed(int64_t seed);
double   duxrt_random(void);
int64_t  duxrt_random_int(int64_t lo, int64_t hi);
double   duxrt_random_double(double lo, double hi);
int32_t  duxrt_random_bool(double p);
void     duxrt_random_shuffle(DuxList* list);

/* ── Threading ───────────────────────────────────────────────────────────── */
void     duxrt_thread_sleep_ms(int64_t ms);
int64_t  duxrt_thread_id(void);
void*    duxrt_thread_self(void);
void*    duxrt_thread_spawn(void* fn, void* arg);
int32_t  duxrt_thread_join(void* handle);
int32_t  duxrt_thread_detach(void* handle);
void*    duxrt_mutex_create(void);
void*    duxrt_mutex_new(void);        /* alias for mutex_create */
void     duxrt_mutex_lock(void* m);
void     duxrt_mutex_unlock(void* m);
int32_t  duxrt_mutex_trylock(void* m);
void     duxrt_mutex_free(void* m);
void*    duxrt_rwlock_new(void);
void     duxrt_rwlock_rlock(void* rw);
void     duxrt_rwlock_wlock(void* rw);
void     duxrt_rwlock_unlock(void* rw);
void     duxrt_rwlock_free(void* rw);
void*    duxrt_cond_new(void);
void     duxrt_cond_wait(void* c, void* m);
void     duxrt_cond_signal(void* c);
void     duxrt_cond_broadcast(void* c);
void     duxrt_cond_free(void* c);
void*    duxrt_once_new(void);
void     duxrt_once_call(void* o, void* fn);
void     duxrt_once_free(void* o);

/* ── Process ─────────────────────────────────────────────────────────────── */
int64_t  duxrt_process_run(DuxStr* cmd);
DuxStr*  duxrt_process_capture(DuxStr* cmd);
int64_t  duxrt_process_pid(void);
int64_t  duxrt_process_ppid(void);
int64_t  duxrt_process_wait(int64_t pid);
int64_t  duxrt_process_spawn(DuxStr* cmd, DuxList* args_list);

/* ── Signals ─────────────────────────────────────────────────────────────── */
int64_t  duxrt_signal_sigint(void);
int64_t  duxrt_signal_sigterm(void);
int64_t  duxrt_signal_sighup(void);
int64_t  duxrt_signal_sigusr1(void);
int64_t  duxrt_signal_sigusr2(void);
int64_t  duxrt_signal_sigchld(void);
int64_t  duxrt_signal_sigpipe(void);
int64_t  duxrt_signal_sigalrm(void);
int32_t  duxrt_signal_ignore(int64_t sig);
int32_t  duxrt_signal_reset(int64_t sig);
int32_t  duxrt_signal_raise(int64_t sig);
int32_t  duxrt_signal_kill(int64_t pid, int64_t sig);

/* ── Bytes ───────────────────────────────────────────────────────────────── */
void*    duxrt_bytes_new(int64_t cap);
void*    duxrt_bytes_from_str(DuxStr* s);
DuxStr*  duxrt_bytes_to_str(void* b);
int64_t  duxrt_bytes_len(void* b);
int64_t  duxrt_bytes_get(void* b, int64_t i);
void     duxrt_bytes_set(void* b, int64_t i, int64_t val);
void     duxrt_bytes_push(void* b, int64_t val);
void     duxrt_bytes_free(void* b);

/* ── JSON ────────────────────────────────────────────────────────────────── */
void*    duxrt_json_parse(DuxStr* input);
DuxStr*  duxrt_json_get_str(void* d, DuxStr* key);
int64_t  duxrt_json_get_int(void* d, DuxStr* key);
double   duxrt_json_get_double(void* d, DuxStr* key);
int32_t  duxrt_json_has(void* d, DuxStr* key);
DuxStr*  duxrt_json_stringify(void* d);

/* ── Regex ───────────────────────────────────────────────────────────────── */
void*    duxrt_regex_compile(DuxStr* pattern);
int32_t  duxrt_regex_matches(void* r, DuxStr* text);
DuxStr*  duxrt_regex_find(void* r, DuxStr* text);
DuxStr*  duxrt_regex_replace(void* r, DuxStr* text, DuxStr* replacement);
DuxList* duxrt_regex_find_all(void* r, DuxStr* text);
void     duxrt_regex_free(void* r);

/* ── Exceptions ──────────────────────────────────────────────────────────── */
typedef struct DuxException {
    const char* type_name;
    char*       message;
    int         line;
    const char* file;
} DuxException;

DuxException* duxrt_exception_new(const char* type, const char* msg);
void          duxrt_exception_free(DuxException* e);

/* Throw a DuxException via the C++ Itanium ABI (__cxa_throw). */
void duxrt_throw(DuxException* e);

#ifdef __cplusplus
} /* extern "C" */
#endif
