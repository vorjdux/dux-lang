# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.3] - 2026-05-26

### Fixed

- **Codegen — `thread_local` globals orphaned in LLVM IR:** Global variables
  declared with `thread_local` were created _after_ function bodies were
  compiled, so every reference inside a function silently fell back to a fresh
  local `alloca`; the `GlobalVariable` nodes existed in the IR but were never
  used. Fixed by adding a pre-pass (Pass 3.5) that declares all
  `thread_local` module-scope globals before function bodies are compiled.
- **Socket — platform-specific `SOL_SOCKET`/`SO_REUSEADDR` constants:**
  The test was calling `set_opt(1, 2, 1)` with Linux-specific numeric constants
  (`SOL_SOCKET=1`, `SO_REUSEADDR=2`); on macOS these are `0xffff`/`4`, causing
  the call to fail silently. Added `duxrt_sock_set_reuseaddr` /
  `duxrt_sock_set_reuseport` runtime helpers that use `<sys/socket.h>` platform
  constants, exposed as `set_reuseaddr()` / `set_reuseport()` on `Socket`.
- **macOS build — missing `<signal.h>` in `process_rt.c`:** `kill()` requires
  `<signal.h>` on macOS; it is not pulled in transitively by `<sys/types.h>`.
- **macOS build — Clang rejects `__extension__` for flexible-array-member
  warning:** Replaced `__extension__ char data[]` with a `#pragma clang
  diagnostic` block in `duxrt.h` so Apple Clang accepts the FAM without
  `-Wc99-extensions`.
- **macOS linker — `-lstdc++` removed in macOS 10.15:** The linker invocation
  in `main.cpp` and `CMakeLists.txt` now uses `-lc++` on Apple platforms and
  `-lstdc++` on Linux.
- **macOS Release build — `-march=native` miscompilation on Apple Silicon:**
  `-march=native` is now opt-in (`-DDUX_MARCH_NATIVE=ON`); it defaults to OFF
  so that CI and distributed binaries are not compiled for a specific M-chip
  micro-architecture.
- **macOS linker — OpenSSL library resolution ambiguity:** When Homebrew's
  `clang-18` is `cc`, bare `-lssl -lcrypto` flags may resolve to the wrong
  LibreSSL stubs. The exact `OPENSSL_SSL_LIBRARY` / `OPENSSL_CRYPTO_LIBRARY`
  paths found by CMake are now baked into the `dux` binary at build time and
  used when linking user programs.
- **Compiler warnings — zero-warning build across GCC 13, Clang 18, and Apple
  Clang:** Eliminated all `-Wall -Wextra -Wpedantic` warnings in source,
  generated parser/lexer, and grammar files.

---

## [0.1.2] - 2026-05-26

### Fixed

- **CI (macOS Apple Silicon):** `llvm@18` was not linked because `llvm@15` was
  already installed; added `brew link --overwrite llvm@18` and added `flex`/`bison`
  bin dirs to `$GITHUB_PATH` so `find_package(FLEX/BISON 3.8)` finds the Homebrew
  versions instead of the ancient macOS system tools.
- **CI (Ubuntu GCC 13 / Clang 18):** Added `llvm-18-dev`, `libssl-dev`, and
  `libzstd-dev` to the apt package list and passed an explicit
  `-DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm` to cmake so LLVM 18 is always
  selected regardless of runner defaults.
- **CI (Fedora 40):** Added `openssl-devel` (required by `find_package(OpenSSL)`)
  and passed `-DLLVM_DIR=/usr/lib64/cmake/llvm18` so cmake finds the versioned LLVM
  18 cmake config files installed by `llvm18-devel`.
- **Merge conflicts** between `release/v0.1.0` and `master` resolved in
  `README.md`, `benchmarks/README.md`, and `docs/spec.md`; spec version bumped
  to 0.2.

### Changed

- Benchmark documentation updated to include all seven tracked workloads with
  results from the typed-list, dict hash-cache, and zero-alloc f-string
  optimisations landed in the perf branch.
- Em dashes replaced with plain hyphens throughout documentation, scripts, and
  configuration files.

---

## [0.1.0] - 2025-05-25

Initial public release of the Dux programming language compiler.

### Added

#### Core language

- Statically typed language with full type inference (`auto` keyword).
- Built-in scalar types: `int` (32-bit), `long` (64-bit), `float`/`real` (32-bit),
  `double` (64-bit), `bool`, `void`, `ptr` (raw pointer for C FFI).
- `str` type: reference-counted string with hybrid inline/heap storage and
  zero-copy slicing.
- `list` generic dynamic array and `dict` open-addressing hash map.
- Typed `int32_t[]` list specialisation that bypasses boxing for integer-only
  sequences.
- F-string interpolation: `f"result={expr}"` with arbitrary expression support.
- Automatic semicolon insertion (Go-style lexer rule) - no trailing semicolons
  required in normal code.
- Literal suffixes: `l` → `long`, `f` → `real`, `d` → `double`.

#### Control flow

- `if` / `else if` / `else` - standard conditional.
- `while` and `do`-`while` loops.
- `for`-`in` loop with range expressions (`0..<n` exclusive, `1..=n` inclusive)
  and the built-in `range(n)` helper.
- C-style `for` loop (`for init, cond, step { ... }`).
- `switch` with `case` / `break` / `default`.
- `match` - exhaustive pattern matching on integers, strings, booleans, and enum
  variants; wildcard arm `_`.
- Labeled `break` (`&label`) for breaking out of outer loops.
- `break` and `continue` in all loop forms.

#### Functions and closures

- Named functions with explicit return types and argument types.
- Generic functions monomorphised at each call site: `T identity<T>(T x)`.
- Arrow-style lambdas: `fn(int n) -> int => n * n`.
- Block-body lambdas for multi-statement closures.
- Closures capture variables from the enclosing scope by value.
- Higher-order functions: `fn(int) -> int` as a first-class parameter type.

#### Classes and object model

- Classes with constructors, per-instance fields, and method dispatch via vtables.
- Single inheritance: `class Dog(Animal)` with parent-constructor call syntax
  (`: Animal(args)`).
- Interfaces (structural typing via vtable slots).
- RAII destructors (`~ClassName()`) called automatically at scope exit; empty
  destructors elided at compile time.
- `defer` statement for LIFO cleanup blocks within a scope.
- Access modifiers: `public:` / `private:` sections within a class body.
- Static fields and static methods (`static int count`, `static void increment()`).
- Static local variables in free functions (retain value between calls).
- Property getters and setters declared with `-> get` / `-> set`.
- Operator overloading via `operator__add`, `operator__eq`, `operator__lt`,
  `operator__index`, etc. (supported: `+` `-` `*` `/` `%` `==` `!=` `<` `>` `<=`
  `>=` `[]`).

#### Generics

- Parametric classes: `class Box<T>`, `class Pair<A, B>`.
- Parametric functions: type parameters in angle brackets.
- All generics resolved via monomorphisation - zero virtual-dispatch overhead per
  instantiation.

#### Enums

- Plain enums with integer-valued variants: `enum Color { Red, Green, Blue }`.
- Enum variants with payloads (sum types).
- Enum variants usable in `match` arms.

#### Async / await and concurrency

- `async` functions and `await` expressions for non-blocking I/O.
- `try` / `catch` exception handling; any class instance can be thrown.
- `catch ...` to catch any thrown value; `catch (TypeName varName)` to bind it.
- Assertions with `assert(expr)`.

#### Namespaces and imports

- `namespace com.example.mylib` declaration.
- `import module` for stdlib modules.
- `import { symbol } from "./path"` for named imports.
- `import "./path"` for full-file imports (names available under module prefix).

#### Unsafe / C FFI

- `extern "C"` declarations for calling C functions directly.
- `unsafe { ... }` blocks for raw-pointer operations and C interop.

#### Standard library modules

- `io` - `println`, `print`, `io.readline`.
- `math` - `sqrt`, `pow`, `floor`, `ceil`, `abs`, `log`, `log2`, `sin`, `cos`,
  `min`, `max`, integer variants (`abs_i`, `min_i`, `max_i`).
- `str` - `length`, `concat`, `slice`, `index`, `eq`, `from_int`, `from_double`,
  `to_upper`, `to_lower`, `trim`, `contains`, `starts_with`, `ends_with`, `find`,
  `replace`, `replace_all`, `repeat`, `ord`, `chr`.
- `list` - generic dynamic array with append, index, and typed int32 specialisation.
- `dict` - open-addressing hash map with FNV-1a hashing.
- `sys` / `sys.sys` / `sys.env` - `pid()`, `ppid()`, `hostname()`.
- `json` - JSON serialisation/deserialisation.
- `regex` - regular expression matching.
- `process` - subprocess spawning and I/O.
- `thread` - `Mutex`, `RWLock`, `CondVar`, `Once`, `thread.id()`,
  `thread.sleep_ms()`.
- `thread.chan` - `Chan` for inter-thread message passing.
- `thread.pool` - `ThreadPool` with configurable worker count.
- `string_builder` - `StringBuilder` with O(1) amortised append and single-alloc
  `build()`.
- `time` - wall-clock and monotonic time helpers.
- `net` - TCP/UDP socket primitives, HTTP client, WebSocket support.
- `async` - async runtime helpers for structured concurrency.

#### Compiler and toolchain

- LLVM backend (LLVM ≥ 17, opaque-pointer IR); uses LLVM native target for
  x86-64, ARM64, and other supported architectures.
- Link-time optimisation (LTO): the compiler merges `duxrt.bc` into the user
  module before optimisation, allowing the inliner full visibility of all runtime
  helper bodies.
- DWARF debug information with `-g`.
- Optimisation levels `-O0` through `-O3`.
- Native object file emission (`--emit-obj`).
- LLVM IR emission (`--emit-ir`) for debugging code generation.
- AST dump (`--dump-ast`).
- Semantic-analysis-only mode (`--check`).
- Interactive REPL (`--repl`).
- Flex/Bison-based lexer and parser with counterexample diagnostics.

#### Bootstrap

- `bootstrap/dux_stage2.dux` (~2 400 lines): a Dux-written lexer and re-printer
  for Dux source, demonstrating real-world program complexity.
- `bootstrap/dux_stage3.dux`: a round-trip test suite that compiles and runs
  stage2, feeds output back into the real compiler, and validates stdout against
  expected values.
- Both programs compile with the production compiler and pass all round-trip
  checks, serving as a language-level dogfood/stress test.

#### Infrastructure

- CMake 3.25+ build system with `Release` and `Debug` configurations.
- Debug builds automatically enable AddressSanitizer and UBSan.
- CPack packaging: `.deb` (Debian/Ubuntu) and `.tar.gz` (generic binary bundle).
- 200-test suite covering: parse-ok, parse-fail, sema-ok, sema-fail, codegen-ok,
  run-ok (with `.expected` stdout comparison), and runtime unit tests.
- CTest integration; optional Valgrind variants.
- GitHub Actions CI matrix: GCC 13, Clang 18, macOS ARM64.
- `compile_commands.json` export for clangd / editor LSP integration.

### Performance

- **Typed int32 list specialisation**: bypasses the generic object-boxing path for
  `list<int>`, yielding an **8.9× speedup** on typed integer list iteration.
- **Dict FNV-1a hash caching**: stores the hash alongside each entry so the
  hot-path probe loop can filter non-matching slots with a single integer compare
  before calling `strcmp`, eliminating string comparison overhead on misses.
- **Zero-alloc f-string integer formatting**: integers formatted inside f-strings
  use a stack-allocated immortal `DuxStr` buffer, bypassing the heap allocator for
  the common case.
- **Benchmark results** at `-O2` with LTO (vs. C gcc -O2):
  - Math loop (50 M iterations): 3 ms - **1.5× C**
  - Recursive fib(35): 31 ms - **1.5× C**
  - String build (50 K appends): 3 ms - **1× C** (equal)
  - Heap alloc/free (1 M cycles): 2 ms - **faster than C**
  - Beats Go on 5 of 7 tracked workloads; within 2× of C on 6 of 7.

[0.1.3]: https://github.com/vorjdux/dux-lang/releases/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/vorjdux/dux-lang/releases/compare/v0.1.0...v0.1.2
[0.1.0]: https://github.com/vorjdux/dux-lang/releases/tag/v0.1.0
