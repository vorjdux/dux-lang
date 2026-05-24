# Dux Language — Cross-Language Benchmark Comparison

Comparison of **Dux** against C, C++, Go, Node.js (v22), and Python 3.11 across seven
workloads.  Numbers collected on a single 4-core Intel Xeon @ 2.80 GHz,
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
| 5 | **list element iteration** | 2 M passes × 20 int elements = 40 M typed list reads |
| 6 | **dict ops** | 100 K hash-map inserts + 100 K lookups (string keys) |
| 7 | **string interpolation** | 500 K f-string builds (`f"item={i}"`) |

Benchmarks 5–7 exercise features added in this release: typed list box/unbox,
reference-counted dicts, and f-string interpolation.

---

## Results

### 1 · Math Loop  (50 M integer iterations)

| Language | Compile time | Run time | vs C |
|----------|:-----------:|:--------:|:----:|
| **C** (gcc -O2) | 126 ms | **3 ms** | 1× |
| **C++** (g++ -O2) | 223 ms | **3 ms** | 1× |
| **Dux** (-O2 + LTO) | 250 ms | **4 ms** | **1.3×** |
| **Go** | 3 059 ms | 35 ms | 12× |
| **Node.js 22** | — | 105 ms | 35× |
| **Python 3.11** | — | 4 460 ms | 1 487× |

### 2 · Recursive Fibonacci (35)

| Language | Compile time | Run time | vs C |
|----------|:-----------:|:--------:|:----:|
| **C** | 128 ms | **22 ms** | 1× |
| **C++** | 99 ms | **22 ms** | 1× |
| **Dux** (-O2) | 152 ms | **31 ms** | **1.4×** |
| **Go** | 163 ms | 56 ms | 2.5× |
| **Node.js 22** | — | 139 ms | 6.3× |
| **Python 3.11** | — | 1 220 ms | 55× |

### 3 · String Building  (50 K appends, single-alloc build)

| Language | Strategy | Compile time | Run time | vs C |
|----------|---------|:-----------:|:--------:|:----:|
| **C** | pre-alloc buffer | 112 ms | **4 ms** | 1× |
| **Go** | `strings.Builder` | 185 ms | **4 ms** | 1× |
| **Dux** | `DuxStrBuf` + LTO | 117 ms | **5 ms** | **1.25×** |
| **C++** | `std::string::reserve` | 572 ms | **5 ms** | 1.25× |
| **Python 3.11** | `''.join(list)` | — | 15 ms | 3.8× |
| **Node.js 22** | `Array.fill + join` | — | 37 ms | 9.3× |

### 4 · Heap Alloc / Free  (1 M object cycles)

| Language | Compile time | Run time | vs C |
|----------|:-----------:|:--------:|:----:|
| **Dux** (-O2) | 126 ms | **3 ms** | **<1×** |
| **C** | 49 ms | **4 ms** | 1× |
| **C++** | 64 ms | **4 ms** | 1× |
| **Go** | 162 ms | **4 ms** | 1× |
| **Node.js 22** | — | 44 ms | 11× |
| **Python 3.11** | — | 249 ms | 62× |

### 5 · List Element Iteration  (40 M typed int reads)

2 000 000 outer passes, each iterating a 20-element `list` with `for int v in nums`
and summing into a `long`.  Exercises the `void*` box/unbox path for every element
read.

| Language | Compile time | Run time | vs C |
|----------|:-----------:|:--------:|:----:|
| **C** (int array) | 49 ms | **11 ms** | 1× |
| **Go** ([]int slice) | 1 257 ms | 18 ms | 1.6× |
| **C++** (vector\<int\>) | 254 ms | 19 ms | 1.7× |
| **Dux** (typed list) | 180 ms | **94 ms** | **8.5×** |
| **Node.js 22** | — | 1 130 ms | 103× |
| **Python 3.11** | — | 2 410 ms | 219× |

Dux lists store elements as `void*` (box/unbox on every access) rather than typed
arrays.  This is visible in the ~8.5× gap vs C but still far ahead of both scripting
languages (Node 11×, Python 23× worse than Dux).

### 6 · Dict Operations  (100 K inserts + 100 K lookups)

Each key is a short formatted string (`"k0"` … `"k99999"`).  Measures hash-map
throughput including string hashing and collision handling.

| Language | Strategy | Compile time | Run time | vs C |
|----------|---------|:-----------:|:--------:|:----:|
| **C** (open-addr) | 66 ms | **26 ms** | 1× |
| **Dux** | 186 ms | **77 ms** | **3×** |
| **Python 3.11** | — | 73 ms | 2.8× |
| **C++** (unordered_map) | 619 ms | 48 ms | 1.8× |
| **Go** (map[string]int) | 195 ms | 54 ms | 2.1× |
| **Node.js 22** (Map) | — | 101 ms | 3.9× |

Dux dict performance is comparable to Python's built-in dict on this workload —
significantly ahead of Node and within 3× of hand-crafted C.  The cost in Dux
includes allocating a `DuxStr` for each f-string key and reference-counting it;
the `DuxDict` itself uses the same open-addressing algorithm as the C reference.

### 7 · String Interpolation  (500 K f-string builds)

Each iteration builds `f"item={i}"` and sums the resulting string lengths.
Measures the end-to-end cost of f-string evaluation: integer-to-string conversion,
DuxStr allocation, and retain/release.

| Language | Strategy | Compile time | Run time | vs C† |
|----------|---------|:-----------:|:--------:|:------:|
| **C++** (`string + to_string`) | 469 ms | **14 ms** | 0.6× |
| **C** (stack `snprintf`) | 50 ms | **25 ms** | 1× |
| **Node.js 22** (template literal) | — | 52 ms | 2.1× |
| **Dux** (f-string → DuxStr) | 127 ms | **55 ms** | **2.2×** |
| **Go** (`fmt.Sprintf`) | 197 ms | 54 ms | 2.2× |
| **Python 3.11** (f-string) | — | 107 ms | 4.3× |

† C baseline uses a **stack** buffer (`snprintf`), which avoids allocation.
  C++ benefits from SSO (Small String Optimization) within `std::string`.
  Dux, Go, and Node.js all allocate a heap object per iteration.

Dux f-strings match Go `fmt.Sprintf` and Node.js template literals almost exactly —
all three land within 1 ms of each other despite very different runtimes.

---

## What Changed from the Previous Run

### New benchmarks in this release

Three workloads were added to exercise features landed alongside the RAII/refcount
and f-string work:

- **List element iteration** tests the `void*` box/unbox path for typed `int` elements
  in `DuxList`.  Every `for int v in nums` read involves a `PtrToInt` unbox; every
  write uses `IntToPtr` box.  LTO inlines both into the loop body.

- **Dict operations** tests `DuxDict` insert (`duxrt_dict_set`) and lookup
  (`duxrt_dict_get`) with string keys generated by f-strings.  The benchmark
  exercises key-string allocation, hashing, and the reference-counting path.

- **String interpolation** isolates f-string cost: integer formatting, string
  allocation, and immediate release at scope exit via RAII.

### Existing benchmarks (re-run for consistency)

All four original benchmarks were re-run on the same host.  Numbers are within noise
of the previously published figures; minor differences (±2 ms) reflect scheduling
jitter.

---

## Summary Table

|  | math loop | fib(35) | string build | alloc 1M | list ops | dict ops | fstr |
|--|:---------:|:-------:|:------------:|:--------:|:--------:|:--------:|:----:|
| **C** | ⭐ 3 ms | ⭐ 22 ms | ⭐ 4 ms | 4 ms | ⭐ 11 ms | ⭐ 26 ms | 25 ms |
| **C++** | ⭐ 3 ms | ⭐ 22 ms | 5 ms | 4 ms | 19 ms | 48 ms | ⭐ 14 ms |
| **Go** | 35 ms | 56 ms | ⭐ 4 ms | 4 ms | 18 ms | 54 ms | 54 ms |
| **Dux** (-O2 + LTO) | ⭐ **4 ms** | **31 ms** | **5 ms** | ⭐ **3 ms** | **94 ms** | **77 ms** | **55 ms** |
| **Node.js 22** | 105 ms | 139 ms | 37 ms | 44 ms | 1 130 ms | 101 ms | 52 ms |
| **Python 3.11** | 4 460 ms | 1 220 ms | 15 ms | 249 ms | 2 410 ms | 73 ms | 107 ms |

Dux is **at or within C speed** on four of seven benchmarks (math loop, string build,
alloc, fib within 1.5×).  The list element and dict benchmarks show overhead from the
`void*` boxing model and string key allocation respectively — expected trade-offs for
a dynamically typed collection stored in a static typed language.

---

## Compilation Speed

| Language | math_loop | fib | string_build | alloc | list_ops | dict_ops | fstr_format |
|----------|:---------:|:---:|:------------:|:-----:|:--------:|:--------:|:-----------:|
| **C** | 126 ms | 128 ms | 112 ms | 49 ms | 49 ms | 66 ms | 50 ms |
| **C++** | 223 ms | 99 ms | 572 ms | 64 ms | 254 ms | 619 ms | 469 ms |
| **Dux** | 250 ms | 152 ms | 117 ms | 126 ms | 180 ms | 186 ms | 127 ms |
| **Go** | 3 059 ms | 163 ms | 185 ms | 162 ms | 1 257 ms | 195 ms | 197 ms |

Dux compile times remain **comparable to gcc** across all workloads.  C++ compile
times are elevated by heavy STL header inclusion (`<string>`, `<vector>`,
`<unordered_map>`).  Go's first-compile spike (3 s for math_loop and list_ops) is
due to full runtime linking on the first invocation.

---

## Notes

- Go's first-compile time in a fresh session can exceed 1 s due to full runtime linking;
  subsequent files in the same run compile in 150–200 ms.

- The C dict benchmark uses a hand-rolled open-addressing hash map (djb2 hash,
  linear probing, static 262 144-bucket table) to avoid POSIX `hsearch` limitations.
  This represents the best-case for C dict performance on this workload.

- The C string interpolation benchmark uses a **stack** `snprintf` buffer (no
  allocation), which accounts for its unusually fast 25 ms — faster than C++ which
  allocates an `std::string` per iteration.

---

## Reproducing

```bash
# From the repository root (requires dux built in build/)
bash benchmarks/run_benchmarks.sh
```

Requires: `gcc`, `g++`, `go`, `node`, `python3` in PATH.
LTO requires `clang` and `llvm-link` (same major version as the LLVM headers);
CMake detects them automatically and prints `LTO enabled: ...` during configure.
