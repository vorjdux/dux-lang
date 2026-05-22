# Dux Language — Cross-Language Benchmark Comparison

Comparison of **Dux** against C, C++, Go, Node.js (v22), and Python 3.11 across four
fundamental workloads.  Numbers collected on a single 4-core Intel Xeon @ 2.80 GHz,
Linux 6.18 (x86-64).  Each runtime is the **best of 3 consecutive runs**.

All compiled languages use the same optimisation level: **-O2**.

> Source files are in `benchmarks/suite/`; runner is `benchmarks/run_benchmarks.sh`.

---

## Benchmarks

| # | Name | Workload |
|---|------|---------|
| 1 | **math loop** | `sum += i * 2 + 1` for i in 0 .. 50 000 000 |
| 2 | **recursive fib** | `fib(35)` with no memoisation |
| 3 | **string build** | 50 000 single-character appends, single-alloc join |
| 4 | **alloc / free** | 1 000 000 two-field object allocation cycles |

---

## Results

### 1 · Math Loop  (50 M integer iterations)

| Language | Compile time | Run time | vs C |
|----------|:-----------:|:--------:|:----:|
| **C** (gcc -O2) | 38 ms | **2 ms** | 1× |
| **C++** (g++ -O2) | 50 ms | **2 ms** | 1× |
| **Dux** (-O2) | 41 ms | **2 ms** | **1×** |
| **Go** | 42 ms | 38 ms | 19× |
| **Node.js 22** | — | 88 ms | 44× |
| **Python 3.11** | — | 3 980 ms | 1 990× |

### 2 · Recursive Fibonacci (35)

| Language | Compile time | Run time | vs C |
|----------|:-----------:|:--------:|:----:|
| **C** | 66 ms | **20 ms** | 1× |
| **C++** | 77 ms | **20 ms** | 1× |
| **Dux** (-O2) | 40 ms | **31 ms** | **1.55×** |
| **Go** | 38 ms | 54 ms | 2.7× |
| **Node.js 22** | — | 127 ms | 6.4× |
| **Python 3.11** | — | 1 150 ms | 58× |

### 3 · String Building  (50 K appends, single-alloc build)

| Language | Strategy | Compile time | Run time | vs C |
|----------|---------|:-----------:|:--------:|:----:|
| **C** | pre-alloc buffer (gcc rewrites as vectorised memset) | 43 ms | **2 ms** | 1× |
| **C++** | `std::string::reserve` | 224 ms | **3 ms** | 1.5× |
| **Go** | `strings.Builder` | 40 ms | **3 ms** | 1.5× |
| **Python 3.11** | `''.join(list)` | — | 13 ms | 6.5× |
| **Node.js 22** | `Array.fill + join` | — | 30 ms | 15× |
| **Dux** | `DuxStrBuf` (pre-alloc contiguous buffer) | 7 ms | 31 ms | 15× |

### 4 · Heap Alloc / Free  (1 M object cycles)

| Language | Compile time | Run time | vs C |
|----------|:-----------:|:--------:|:----:|
| **C** | 41 ms | **2 ms** | 1× |
| **C++** | 50 ms | **3 ms** | 1.5× |
| **Dux** (-O2) | 40 ms | **2 ms** | **1×** |
| **Go** ¹ | 38 ms | 4 ms | 2× |
| **Node.js 22** | — | 37 ms | 18× |
| **Python 3.11** | — | 212 ms | 106× |

---

## What Changed from the First Run

The initial benchmark report contained three systematic problems.  Fixing them put
Dux in the same performance tier as C for three out of four workloads.

### Fix A — Optimisation level parity

The first run compiled C and C++ with `-O2` but Dux with `-O0` (the implicit default).
At `-O2`, LLVM auto-vectorises the math loop into a single SIMD instruction — the
same pass that gives C its 2 ms result.  Adding `-O2` to the Dux compile command
dropped the math loop from **125 ms → 2 ms** (62×), making it exactly equal to C.

The fibonacci result also collapsed: **53 ms → 31 ms**, now within 1.55× of C.  LLVM's
inliner removes the recursive call overhead at `-O2`.

### Fix B — Empty destructor elimination

The alloc benchmark uses `~Node() {}` — an empty destructor body.  The old codegen
generated a real `Node___dtor` function (a no-op that just returned) and called it
on every `delete`.  1 000 000 empty-function calls is measurable overhead.

The fix: if the destructor body has no statements, skip generating the `___dtor`
symbol entirely.  `emit_dtor` detects the missing symbol and emits a direct `free()`
without the call overhead.  The RAII registration condition was also updated from
"has a dtor symbol" to "has a class layout" so trivial-dtor objects are still freed
at scope exit.

Result: alloc went from **15 ms → 2 ms** — equal to C.

### Fix C — O(n²) string building → DuxStrBuf

The original `string_build.dux` used `s = s + "x"` in a loop.  Each iteration
allocates a new string of length _n+1_ and copies all previous characters.  For
50 000 iterations that is 1.25 billion bytes of copying — fundamentally O(n²).

**First fix:** `duxrt_str_join_list` — accumulate `DuxStr*` pointers in a `DuxList`,
then join in one two-pass O(n) allocation.  This dropped the time from the O(n²)
baseline to **35 ms**, but still incurred per-element `duxrt_list_push` heap overhead.

**Second fix:** `DuxStrBuf` — a pre-allocated growing `char*` buffer backed by
`realloc`-doubling (like `std::string::reserve`).  Each append is a bounds check +
`memcpy` into a contiguous buffer; no per-element heap allocation.  A single
`duxrt_str_new()` materialises the result at the end.

Result: string build dropped from **35 ms → 31 ms** with the contiguous buffer.

`stdlib/string_builder.dux` exposes `DuxStrBuf` as a `StringBuilder` class for
general use.

---

## Summary Table

|  | math loop | fib(35) | string build | alloc 1M |
|--|:---------:|:-------:|:------------:|:--------:|
| **C** | ⭐ 2 ms | ⭐ 20 ms | ⭐ 2 ms | ⭐ 2 ms |
| **C++** | ⭐ 2 ms | ⭐ 20 ms | 3 ms | 3 ms |
| **Go** | 38 ms | 54 ms | ⭐ 3 ms | 4 ms |
| **Dux** (-O2) | ⭐ **2 ms** | **31 ms** | 31 ms | ⭐ **2 ms** |
| **Node.js 22** | 88 ms | 127 ms | 30 ms | 37 ms |
| **Python 3.11** | 3 980 ms | 1 150 ms | 13 ms | 212 ms |

Dux is now **equal to C** on two of four benchmarks and competitive on a third
(fibonacci, 1.55×).  The string-build gap is structural and explained below.

---

## Compilation Speed

| Language | math_loop | fib | string_build | alloc |
|----------|:---------:|:---:|:------------:|:-----:|
| **C** | 38 ms | 66 ms | 43 ms | 41 ms |
| **C++** | 50 ms | 77 ms | 224 ms ² | 50 ms |
| **Dux** | 41 ms | 40 ms | 7 ms | 40 ms |
| **Go** | 42 ms | 38 ms | 40 ms | 38 ms |

Dux compile times are **comparable to gcc** across all files.  The compiler uses
LLVM as a backend, so `-O2` adds only a small overhead on single-file programs
relative to the front-end parse and codegen pass.

The notable outlier is Dux's **7 ms** for `string_build` — the benchmark uses only
`extern "C"` declarations and a `while` loop with no class instantiation, so the
front-end work is minimal.

---

## Remaining Gap: String Building

Dux's 31 ms vs C's 2 ms comes from a structural difference in how the compiler
sees the hot loop, not from an algorithmic deficiency.

### What gcc does to the C benchmark

`string_build.c` contains a simple `buf[i] = 'x'` loop over a pre-allocated
buffer.  At `-O2`, gcc recognises the uniform byte-write pattern and replaces the
entire loop with **two `memset` calls** (one to zero the range, one to fill with
`'x'`), both of which are SIMD-vectorised by the C standard library.  The result is
a handful of AVX2 stores across 50 KB — completing in ~2 ms regardless of iteration
count.

### Why Dux cannot match that

The Dux benchmark calls `duxrt_strbuf_append_str(buf, "x")` 50 000 times.  This
function lives in a separately compiled C translation unit (`src/runtime/str.c`).
LLVM sees it as an **opaque `extern` symbol** — it cannot inline the body, inspect
the memory-access pattern, or fuse the 50 000 calls into a memset.  Each call
incurs full function-call overhead: argument setup, call instruction, return.

The C++ result (3 ms) shows how much inlining helps: `std::string::operator+=` for
`char` is fully inlined by g++ and the loop is auto-vectorised in-place.  Go's
`strings.Builder.WriteByte` is similarly inlined by the Go compiler.

### The fix

The gap is addressable by one of:

1. **LTO** — link-time optimisation allows LLVM to inline `duxrt_strbuf_append_str`
   at link time, exposing the `memcpy` body to the auto-vectoriser.
2. **Intrinsic lowering** — treat `StringBuilder.append` as a compiler-known
   operation and emit inline LLVM IR (a `getelementptr` + `store` + counter
   increment) instead of a function call.
3. **String interpolation** — for the common case of building a string from a
   fixed set of parts known at compile time, the compiler can emit a single
   `duxrt_str_new` with a pre-computed length.

All three are future optimisations; the algorithm is already O(n)-correct.

---

## Notes

¹ Go's alloc benchmark benefits from escape analysis: the `&Node{i, i+1}` literal
  may be stack-allocated when the function is inlined, avoiding heap entirely.

² C++ compile time for `string_build.cpp` is higher because `<string>` pulls in
  a large portion of the STL headers.

---

## Reproducing

```bash
# From the repository root (requires dux built in build/)
bash benchmarks/run_benchmarks.sh
```

Requires: `gcc`, `g++`, `go`, `node`, `python3` in PATH.
