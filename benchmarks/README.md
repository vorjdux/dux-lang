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
| **C** (gcc -O2) | 55 ms | **3 ms** | 1× |
| **C++** (g++ -O2) | 82 ms | **3 ms** | 1× |
| **Dux** (-O2) | 57 ms | **3 ms** | **1×** |
| **Go** | 624 ms | 36 ms | 12× |
| **Node.js 22** | — | 115 ms | 38× |
| **Python 3.11** | — | 6 270 ms | 2 090× |

### 2 · Recursive Fibonacci (35)

| Language | Compile time | Run time | vs C |
|----------|:-----------:|:--------:|:----:|
| **C** | 88 ms | **29 ms** | 1× |
| **C++** | 112 ms | **28 ms** | ~1× |
| **Dux** (-O2) | 60 ms | **31 ms** | **1.07×** |
| **Go** | 60 ms | 55 ms | 1.9× |
| **Node.js 22** | — | 201 ms | 6.9× |
| **Python 3.11** | — | 1 500 ms | 52× |

### 3 · String Building  (50 K appends, O(n) join)

| Language | Strategy | Compile time | Run time | vs C |
|----------|---------|:-----------:|:--------:|:----:|
| **C** | pre-alloc buffer | 70 ms | **3 ms** | 1× |
| **C++** | `std::string::reserve` | 349 ms | 4 ms | 1.3× |
| **Go** | `strings.Builder` | 62 ms | **3 ms** | 1× |
| **Python 3.11** | `''.join(list)` | — | 15 ms | 5× |
| **Dux** | list + `duxrt_str_join_list` | 8 ms | 35 ms | 12× |
| **Node.js 22** | `Array.join` | — | 43 ms | 14× |

### 4 · Heap Alloc / Free  (1 M object cycles)

| Language | Compile time | Run time | vs C |
|----------|:-----------:|:--------:|:----:|
| **C** | 63 ms | **3 ms** | 1× |
| **C++** | 74 ms | **3 ms** | 1× |
| **Go** ¹ | 70 ms | 4 ms | 1.3× |
| **Dux** (-O2) | 54 ms | **3 ms** | **1×** |
| **Node.js 22** | — | 48 ms | 16× |
| **Python 3.11** | — | 321 ms | 107× |

---

## What Changed from the First Run

The initial benchmark report contained three systematic problems.  Fixing them put
Dux in the same performance tier as C for three out of four workloads.

### Fix A — Optimisation level parity

The first run compiled C and C++ with `-O2` but Dux with `-O0` (the implicit default).
At `-O2`, LLVM auto-vectorises the math loop into a single SIMD instruction — the
same pass that gives C its 3 ms result.  Adding `-O2` to the Dux compile command
dropped the math loop from **125 ms → 3 ms** (42×), making it exactly equal to C.

The fibonacci result also collapsed: **53 ms → 31 ms**, now within 7% of C.  LLVM's
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

Result: alloc went from **15 ms → 3 ms** — equal to C.

### Fix C — O(n²) string building → O(n) join

The old `string_build.dux` used `s = s + "x"` in a loop.  Each iteration allocates
a new string of length _n+1_ and copies all previous characters.  For 50 000
iterations that is 1.25 billion bytes of copying — fundamentally O(n²).

The fix adds `duxrt_str_join_list(DuxList* parts, int64_t count)` to the runtime:
it sums all part lengths in one pass, allocates a single output buffer, then copies
each part once — O(n) in total characters.  `string_build.dux` now pushes each
character as an immortal string literal into a list and calls the join function once
at the end.

`stdlib/string_builder.dux` provides a `StringBuilder` class that wraps the same
pattern for general use.

---

## Summary Table

|  | math loop | fib(35) | string build | alloc 1M |
|--|:---------:|:-------:|:------------:|:--------:|
| **C** | ⭐ 3 ms | ⭐ 29 ms | ⭐ 3 ms | ⭐ 3 ms |
| **C++** | ⭐ 3 ms | ⭐ 28 ms | 4 ms | ⭐ 3 ms |
| **Go** | 36 ms | 55 ms | ⭐ 3 ms | 4 ms |
| **Dux** (-O2) | ⭐ **3 ms** | ⭐ **31 ms** | 35 ms | ⭐ **3 ms** |
| **Node.js 22** | 115 ms | 201 ms | 43 ms | 48 ms |
| **Python 3.11** | 6 270 ms | 1 500 ms | 15 ms | 321 ms |

Dux is now **equal to C** on three of four benchmarks and within 12× on the
fourth (string building, where the remaining gap is runtime list overhead vs a
pre-allocated C buffer, not a language-level issue).

---

## Compilation Speed

| Language | math_loop | fib | string_build | alloc |
|----------|:---------:|:---:|:------------:|:-----:|
| **C** | 55 ms | 88 ms | 70 ms | 63 ms |
| **C++** | 82 ms | 112 ms | 349 ms ² | 74 ms |
| **Dux** | 57 ms | 60 ms | 8 ms | 54 ms |
| **Go** | 624 ms ³ | 60 ms | 62 ms | 70 ms |

Dux compile times are **comparable to gcc** across all files.  The compiler uses
LLVM as a backend, so `-O2` adds only a small overhead on single-file programs
relative to the front-end parse and codegen pass.

---

## Remaining Gap: String Building

Dux's 35 ms vs C's 3 ms (12×) comes from two sources:

1. **List push overhead** — 50 000 calls to `duxrt_list_push` (which may `realloc`
   as the list grows) vs a simple in-place buffer increment in C.
2. **Per-element overhead in join** — `duxrt_str_join_list` calls `duxrt_list_get`
   (bounds-checked) for each element; C uses direct pointer arithmetic.

Both are addressable with a dedicated `StringBuffer` runtime type backed by a
growing `char*` rather than a `DuxList` of `DuxStr*` pointers.  The algorithm is
already correct; the constant factor is a future optimisation.

---

## Notes

¹ Go's alloc benchmark benefits from escape analysis: the `&Node{i, i+1}` literal
  may be stack-allocated when the function is inlined, avoiding heap entirely.

² C++ compile time for `string_build.cpp` is higher because `<string>` pulls in
  a large portion of the STL headers.

³ Go first-compile time is elevated (~624 ms for `math_loop.go`) due to linking the
  full Go runtime; subsequent files compile in 60–70 ms.

---

## Reproducing

```bash
# From the repository root (requires dux built in build/)
bash benchmarks/run_benchmarks.sh
```

Requires: `gcc`, `g++`, `go`, `node`, `python3` in PATH.
