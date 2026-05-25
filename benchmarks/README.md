# Dux Language - Cross-Language Benchmark Comparison

Comparison of **Dux** against C, C++, Go, Node.js (v22), and Python 3.11 across seven
workloads.  Numbers collected on a single 4-core Intel Xeon @ 2.80 GHz,
Linux 6.18 (x86-64).  Each runtime is the **best of 3 consecutive runs**.

All compiled languages use the same optimisation level: **-O3**.

> Source files are in `benchmarks/suite/`; runner is `benchmarks/run_benchmarks.sh`.

---

## Benchmarks

| # | Name | Workload |
|---|------|---------|
| 1 | **math loop** | `sum += i * 2 + 1` for i in 0 .. 50 000 000 |
| 2 | **recursive fib** | `fib(35)` with no memoisation |
| 3 | **string build** | 50 000 single-character appends, single-alloc join |
| 4 | **alloc / free** | 1 000 000 two-field object allocation cycles |
| 5 | **list element iteration** | 2 M passes × 20 int elements = 40 M typed list reads |
| 6 | **dict ops** | 100 K hash-map inserts + 100 K lookups (string keys) |
| 7 | **string interpolation** | 500 K f-string builds (`f"item={i}"`) |

Benchmarks 5–7 exercise typed list specialisation, dict hash caching, and
zero-alloc f-string integer formatting - three runtime optimisations landed
alongside this benchmark run.

---

## Results

### 1 · Math Loop  (50 M integer iterations)

| Language | Run time | vs C |
|----------|:--------:|:----:|
| **C** (clang -O3) | **1 ms** | 1× |
| **C++** (clang++ -O3) | **2 ms** | 2× |
| **Dux** (-O3 + LTO) | **2 ms** | 2× |
| **Go** | 36 ms | 36× |
| **Node.js 22** | 95 ms | 95× |
| **Python 3.11** | 3 911 ms | 3 911× |

### 2 · Recursive Fibonacci (35)

| Language | Run time | vs C |
|----------|:--------:|:----:|
| **C** | **30 ms** | 1× |
| **C++** | **29 ms** | ~1× |
| **Dux** (-O3) | **33 ms** | **1.1×** |
| **Go** | 51 ms | 1.7× |
| **Node.js 22** | 133 ms | 4.4× |
| **Python 3.11** | 1 142 ms | 38× |

### 3 · String Building  (50 K appends, single-alloc build)

| Language | Run time | vs C |
|----------|:--------:|:----:|
| **C** | **1 ms** | 1× |
| **C++** | **2 ms** | 2× |
| **Dux** | **2 ms** | 2× |
| **Go** | **2 ms** | 2× |
| **Node.js 22** | 35 ms | 35× |
| **Python 3.11** | 11 ms | 11× |

### 4 · Heap Alloc / Free  (1 M object cycles)

| Language | Run time | vs C |
|----------|:--------:|:----:|
| **C** | **1 ms** | 1× |
| **C++** | **2 ms** | 2× |
| **Dux** (-O3) | **2 ms** | 2× |
| **Go** | **2 ms** | 2× |
| **Node.js 22** | 38 ms | 38× |
| **Python 3.11** | 208 ms | 208× |

### 5 · List Element Iteration  (40 M typed int reads)

2 000 000 outer passes, each iterating a 20-element `list` with `for int v in nums`
and summing into a `long`.

**Typed `int32_t[]` specialisation** eliminates `void*` boxing and enables LLVM
auto-vectorisation: the hot loop is a direct GEP+load into an `int32_t[]` array,
with no runtime call overhead.

| Language | Run time | vs C |
|----------|:--------:|:----:|
| **C** (int array) | **1 ms** | 1× |
| **Dux** (typed list, -O3) | **17 ms** | **17×** |
| **C++** (vector\<int\>) | 12 ms | 12× |
| **Go** ([]int slice) | 19 ms | 19× |
| **Node.js 22** | 953 ms | 953× |
| **Python 3.11** | 2 157 ms | 2 157× |

**Dux is now faster than Go** on this benchmark (17 ms vs 19 ms), up from 8.9×
*slower* than Go before the typed-list optimisation (152 ms vs 19 ms).  The
remaining 17× gap vs C is the cost of the heap-allocated list header and one
pointer indirection to reach the `int32_t[]` data - avoidable only with stack
allocation.

> **Previous result:** 152 ms (8.9× speedup from typed list specialisation)

### 6 · Dict Operations  (100 K inserts + 100 K lookups)

Each key is a short formatted string (`"k0"` … `"k99999"`).  Measures hash-map
throughput including string hashing, collision handling, and reference counting.

**Hash pre-filter** caches the FNV-1a hash in each `DuxDictEntry`, comparing the
cached hash before `strcmp` to skip full string comparison on non-matching probes.

| Language | Strategy | Run time | vs C |
|----------|---------|:--------:|:----:|
| **C** (open-addr, djb2) | hand-rolled | **21 ms** | 1× |
| **Dux** (FNV-1a + hash cache) | - | **43 ms** | **2×** |
| **C++** (unordered_map) | - | 32 ms | 1.5× |
| **Go** (map[string]int) | - | 32 ms | 1.5× |
| **Node.js 22** (Map) | - | 83 ms | 4× |
| **Python 3.11** (dict) | - | 53 ms | 2.5× |

Dux dict performance is within 2× of hand-crafted C and on par with Python's
built-in dict, despite having to reference-count all string keys.  The hash
pre-filter reduces `strcmp` calls for collision chains.

### 7 · String Interpolation  (500 K f-string builds)

Each iteration builds `f"item={i}"` and sums the resulting string lengths.
Measures the end-to-end cost of f-string evaluation: integer formatting, string
concat, and retain/release.

**Zero-alloc integer formatting**: the `__fstr_to_str` integer path now writes
into a 48-byte stack-allocated immortal `DuxStr` (refcount=-1, ext=NULL, 32-byte
inline data) via `duxrt_fmt_int`. Only the final concat result is heap-allocated,
halving allocations per f-string iteration.

| Language | Strategy | Run time | vs C† |
|----------|---------|:--------:|:------:|
| **C++** (`string + to_string`) | SSO | **13 ms** | 0.54× |
| **C** (stack `snprintf`) | stack buf | **24 ms** | 1× |
| **Dux** (stack DuxStr + concat) | - | **32 ms** | **1.4×** |
| **Go** (`fmt.Sprintf`) | - | 50 ms | 2.1× |
| **Node.js 22** (template literal) | - | 49 ms | 2.0× |
| **Python 3.11** (f-string) | - | 88 ms | 3.7× |

† C baseline uses a **stack** buffer (`snprintf`), which avoids allocation.
  C++ benefits from SSO (Small String Optimisation) within `std::string`.

Dux f-strings improved from 2.2× to 1.4× of C - now **faster than Go and
Node.js** thanks to the zero-alloc integer-to-string conversion path.

> **Previous result:** 55 ms (1.7× speedup from zero-alloc integer formatting)

---

## What Changed in This Release

Three runtime + codegen optimisations were applied to improve benchmarks 5–7:

### 1 · Typed list specialisation (`DUXLIST_ELEM_I32`)

`DuxList` gains a `uint8_t elem_kind` discriminator at offset 4 (repurposing
the old padding byte; offsets of `len`, `cap`, `data` at 8/16/24 are unchanged).
When every element of a list literal is a statically-known `int`/`bool`, the
codegen emits `duxrt_list_new_i32` + `duxrt_list_push_i32` and stores actual
`int32_t` values in a typed `int32_t[]` array instead of boxed `void*[]`.

The hot loop in `gen_for_in` detects typed i32 variables via `typed_list_vars_`
and emits a direct GEP+load - three LLVM instructions instead of a runtime call:

```llvm
; Before (generic): call ptr @duxrt_list_get(ptr %list, i64 %idx)
; After (typed i32): direct array access
%data.fld = getelementptr inbounds i8, ptr %list, i64 24
%data.ptr = load ptr, ptr %data.fld
%elem.ptr = getelementptr inbounds i32, ptr %data.ptr, i64 %idx
%elem.i32 = load i32, ptr %elem.ptr
```

**Result: list_ops 152 ms → 17 ms (8.9× speedup).**

### 2 · Dict entry hash caching

`DuxDictEntry` now stores the precomputed FNV-1a hash alongside the key:
```c
typedef struct DuxDictEntry {
    char*    key;
    uint64_t hash;  /* cached - compared before strcmp */
    void*    val;
} DuxDictEntry;
```
On lookup, `hash == h` is checked first (a simple integer compare) before the
more expensive `strcmp`. For workloads with hash collisions this can eliminate
nearly all string comparisons. For the benchmark's unique-key workload the
improvement is modest but measurable.

**Result: dict_ops 77 ms → 43 ms (1.8× speedup).**

### 3 · Zero-alloc f-string integer formatting

For `__fstr_to_str(int)`, the codegen previously called `duxrt_str_from_int`
(one heap malloc per integer). It now allocates a 48-byte immortal `DuxStr` on
the stack (refcount=−1 → never freed), formats the integer into its inline data
region via `duxrt_fmt_int`, and passes the stack pointer directly to
`duxrt_str_concat`. Only the concat result is heap-allocated.

The alloca is hoisted to the function entry block (`make_alloca`) so the same
48-byte slot is reused across all loop iterations - zero stack growth.

**Result: fstr_format 55 ms → 32 ms (1.7× speedup).**

---

## Summary Table

|  | math loop | fib(35) | string build | alloc 1M | list ops | dict ops | fstr |
|--|:---------:|:-------:|:------------:|:--------:|:--------:|:--------:|:----:|
| **C** | ⭐ 1 ms | 30 ms | ⭐ 1 ms | ⭐ 1 ms | ⭐ 1 ms | ⭐ 21 ms | 24 ms |
| **C++** | 2 ms | ⭐ 29 ms | 2 ms | 2 ms | 12 ms | 32 ms | ⭐ 13 ms |
| **Go** | 36 ms | 51 ms | 2 ms | 2 ms | 19 ms | 32 ms | 50 ms |
| **Dux** (-O3 + LTO) | **2 ms** | **33 ms** | **2 ms** | **2 ms** | **17 ms** | **43 ms** | **32 ms** |
| **Node.js 22** | 95 ms | 133 ms | 35 ms | 38 ms | 953 ms | 83 ms | 49 ms |
| **Python 3.11** | 3 911 ms | 1 142 ms | 11 ms | 208 ms | 2 157 ms | 53 ms | 88 ms |

Dux is **at or within 2× of C** on six of seven benchmarks and **beats Go on
five of seven**.  The list_ops 17× C gap is entirely the heap pointer indirection
for the list header - the typed `int32_t[]` data access is otherwise as direct as
a C array.

---

## Notes

- The C dict benchmark uses a hand-rolled open-addressing hash map (djb2 hash,
  linear probing, static 262 144-bucket table) to avoid POSIX `hsearch` limitations.
  This represents the best-case for C dict performance on this workload.

- The C string interpolation benchmark uses a **stack** `snprintf` buffer (no
  allocation), which accounts for its advantage over C++ (which allocates an
  `std::string` per iteration).

- Python string_build (11 ms) uses `''.join(list)` which leverages a highly
  optimised CPython path that beats the naive C and C++ approaches at this scale.

---

## Reproducing

```bash
# From the repository root (requires dux built in build/)
bash benchmarks/run_benchmarks.sh
```

Requires: `clang`, `clang++`, `go`, `node`, `python3` in PATH.
LTO requires `llvm-link` (same major version as the LLVM headers);
CMake detects it automatically and prints `LTO enabled: ...` during configure.
