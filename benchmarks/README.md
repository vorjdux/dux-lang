# Dux Language — Cross-Language Benchmark Comparison

Comparison of **Dux** against C, C++, Go, Node.js (v22), and Python 3.11 across four
fundamental workloads.  Numbers were collected on a single 4-core Intel Xeon @ 2.80 GHz
running Linux 6.18 (x86-64).  Each runtime was taken as the **best of 3 consecutive runs**
to reduce OS scheduling noise.

> **Goal:** an honest snapshot of where Dux sits today, not a comprehensive language shootout.
> All source files are in `benchmarks/suite/`; the runner is `benchmarks/run_benchmarks.sh`.

---

## Benchmarks

| # | Name | Workload |
|---|------|---------|
| 1 | **math loop** | `sum += i * 2 + 1` for i in 0 .. 50 000 000 |
| 2 | **recursive fib** | `fib(35)` with no memoisation |
| 3 | **string build** | 50 000 single-character appends |
| 4 | **alloc / free** | 1 000 000 two-field object allocation cycles |

---

## Results

### 1 · Math Loop  (50 M integer iterations)

| Language | Compile time | Run time | vs C |
|----------|:-----------:|:--------:|:----:|
| **C** (gcc -O2) | 229 ms | **3 ms** | 1× |
| **C++** (g++ -O2) | 201 ms | **3 ms** | 1× |
| **Go** | 6 380 ms ¹ | 34 ms | 11× |
| **Dux** | 2 290 ms ² | 125 ms | 42× |
| **Node.js 22** | — | 97 ms | 32× |
| **Python 3.11** | — | 4 380 ms | 1 460× |

### 2 · Recursive Fibonacci (35)

| Language | Compile time | Run time | vs C |
|----------|:-----------:|:--------:|:----:|
| **C** | 71 ms | **22 ms** | 1× |
| **C++** | 97 ms | **21 ms** | ~1× |
| **Dux** | 43 ms | **53 ms** | 2.4× |
| **Go** | 154 ms | 54 ms | 2.5× |
| **Node.js 22** | — | 134 ms | 6× |
| **Python 3.11** | — | 1 190 ms | 54× |

### 3 · String Building  (50 K appends)

| Language | Strategy | Compile time | Run time | vs C |
|----------|---------|:-----------:|:--------:|:----:|
| **C** | pre-alloc buffer | 51 ms | **3 ms** | 1× |
| **C++** | `std::string::reserve` | 268 ms | 4 ms | 1.3× |
| **Go** | `strings.Builder` | 254 ms | 3 ms | 1× |
| **Python 3.11** | `''.join(list)` | — | 13 ms | 4× |
| **Dux** | `s = s + "x"` (copy) | 44 ms | 30 ms | 10× |
| **Node.js 22** | `Array.join` | — | 34 ms | 11× |

### 4 · Heap Alloc / Free  (1 M object cycles)

| Language | Compile time | Run time | vs C |
|----------|:-----------:|:--------:|:----:|
| **C** | 45 ms | **3 ms** | 1× |
| **C++** | 53 ms | **3 ms** | 1× |
| **Go** ³ | 151 ms | 3 ms | 1× |
| **Dux** | 39 ms | 15 ms | 5× |
| **Node.js 22** | — | 36 ms | 12× |
| **Python 3.11** | — | 250 ms | 83× |

---

## Observations

### Compilation speed

Dux compilation of a small single-file program takes **39 – 44 ms** once the LLVM
toolchain is warm, which is **comparable to a plain `gcc` compile**.  The first
cold invocation in this session was ~2.3 s — almost entirely LLVM library loading
and IR codegen for larger files.  Go's first compile was surprisingly slow (~6.4 s
for `math_loop.go`) due to first-time runtime linkage; subsequent files came in
at 150 – 250 ms.

### Math loop

C and C++ dominate because `-O2` auto-vectorises the trivial loop into a single
SIMD instruction.  Dux emits unoptimised IR at `-O0` (the default); adding `-O2`
to the Dux build would narrow this gap considerably.  Even unoptimised, Dux is
only **1.3× slower than Node.js** — not bad for a young compiled language.

### Recursive fibonacci

This is the benchmark where Dux shines: **53 ms**, essentially identical to Go
(54 ms).  Function call overhead and integer arithmetic in Dux are already
on par with a mature compiled language.  Both are ~2.4× slower than C, which
benefits from deeper inlining at `-O2`.

### String building

Dux is **10× slower than C** here, but the comparison is not entirely fair:
C pre-allocates the full buffer in one `malloc`; Dux uses copy-on-concat
semantics (`s = s + "x"` copies the whole string each time, making the total
work O(n²)).  A future `StringBuilder` class or a `strings.Builder`-style API
would close most of this gap.  Node.js uses `Array.join` which also avoids
incremental copies, but still lands at 34 ms — showing that V8 startup overhead
dominates for this workload size.

### Heap allocation

Dux's **15 ms** for 1 M alloc/free cycles includes RAII destructor dispatch
(null-guard check + vtable lookup) on every `delete`.  C and C++ have no such
overhead, and Go's escape analysis keeps most of the objects on the stack,
avoiding heap entirely.  Dux's overhead is still **2.4× faster than Node.js**
and **17× faster than Python**.

---

## Summary Table

| | math loop | fib(35) | string build | alloc 1M |
|---|:---------:|:-------:|:------------:|:--------:|
| **C** | ⭐ 3 ms | ⭐ 22 ms | ⭐ 3 ms | ⭐ 3 ms |
| **C++** | ⭐ 3 ms | ⭐ 21 ms | 4 ms | ⭐ 3 ms |
| **Go** | 34 ms | 54 ms | ⭐ 3 ms | ⭐ 3 ms |
| **Dux** | 125 ms | 53 ms | 30 ms | 15 ms |
| **Node.js 22** | 97 ms | 134 ms | 34 ms | 36 ms |
| **Python 3.11** | 4 380 ms | 1 190 ms | 13 ms | 250 ms |

Dux's sweet spot today is **recursive / call-heavy numeric code**, where it
matches Go.  The areas with most room for improvement are tight arithmetic loops
(would benefit from `-O2` codegen by default) and string operations (copy
semantics make repeated concatenation O(n²)).

---

## Notes

¹ Go first-compile time is high (~6.4 s for `math_loop.go`) because it links the
  full Go runtime the first time; subsequent files compile in 150 – 250 ms.

² Dux first-compile time (~2.3 s) includes LLVM library startup cost; per-file
  compile on warm toolchain is 39 – 44 ms (faster than `gcc` on these files).

³ Go's alloc benchmark likely benefits from escape analysis promoting the `Node`
  struct to the stack, avoiding heap allocation entirely.

---

## Reproducing

```bash
# From the repository root (requires dux built in build/)
bash benchmarks/run_benchmarks.sh
```

Requires: `gcc`, `g++`, `go`, `node`, `python3` in PATH.
