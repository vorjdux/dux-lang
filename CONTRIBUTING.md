# Contributing to Dux

Thank you for your interest in contributing to the Dux programming language
compiler.  This guide covers how to build the project, run the test suite,
add new tests, and extend the standard library.

---

## 1. Build instructions

### Prerequisites

| Tool | Version |
|---|---|
| CMake | >= 3.25 |
| C++ compiler | C++23 (GCC 13+ or Clang 16+) |
| Flex | >= 2.6 |
| Bison | >= 3.8 |
| LLVM | 18 |

On Ubuntu / Debian:

```bash
sudo apt install cmake flex bison g++-13 llvm-18-dev clang-18
```

`clang-18` is optional but enables LTO at `-O2`+.

### Debug build (with AddressSanitizer + UBSan)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

### Release build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The resulting compiler binary is at `build/dux`.

### Running a single file manually

```bash
./build/dux --compile examples/euler12.dux -o euler12
./euler12
```

---

## 2. Running the test suite

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

To run a specific category (e.g. only parse tests):

```bash
ctest --test-dir build -R "^parse_" --output-on-failure
```

To see verbose output for a single test:

```bash
ctest --test-dir build -R "parse_ok_asi_basic" -V
```

### Test categories

| Category | Directory | What it checks |
|---|---|---|
| `parse_ok` | `tests/parse_ok/` | Source files that must parse without errors |
| `parse_fail` | `tests/parse_fail/` | Source files that must produce a parse error |
| `sema_ok` | `tests/sema_ok/` | Source files that must pass semantic analysis |
| `sema_fail` | `tests/sema_fail/` | Source files that must produce a semantic error |
| `codegen_ok` | `tests/codegen_ok/` | Source files that must compile to LLVM IR without errors |
| `run_ok` | `tests/run_ok/` | Programs that must compile, run, and produce the expected output |
| `run_fail` | `tests/run_fail/` | Programs that must compile and then exit with a non-zero status |

**`run_ok` tests** require a companion `.expected` file in the same directory.
The test runner compiles the `.dux` file, runs the resulting binary, and
compares its stdout to the `.expected` file byte-for-byte.

**`run_fail` tests** require that the compiled binary exits with a non-zero
exit code (e.g. a failed `assert`, an unhandled exception, or a deliberate
`exit(1)`).  No `.expected` file is needed.

**`parse_fail` and `sema_fail` tests** require that the compiler exits with a
non-zero status and writes at least one diagnostic to stderr.

---

## 3. Adding new tests

### parse_ok

Place a `.dux` file in `tests/parse_ok/`.  The file must parse cleanly under
`dux --dump-ast`.  CMake discovers all `*.dux` files in this directory
automatically — no CMakeLists.txt edit is required.

Naming convention: `<feature>_<scenario>.dux`, for example
`generics_bounds.dux` or `asi_multiline_call.dux`.

### parse_fail

Place a `.dux` file in `tests/parse_fail/`.  The file must contain at least
one syntax error that the parser reports.  CMake discovers it automatically.

### sema_ok / sema_fail

Place a `.dux` file in `tests/sema_ok/` or `tests/sema_fail/` respectively.
Follow the same naming convention as `parse_ok`.

### codegen_ok

Place a `.dux` file in `tests/codegen_ok/`.  The file must compile to LLVM IR
without errors (`dux --emit-ir`).

### run_ok

1. Place the `.dux` source in `tests/run_ok/`.
2. Create a matching `tests/run_ok/<name>.expected` file containing the exact
   expected stdout of the program (including trailing newline if any).

Example:

```
tests/run_ok/closures.dux
tests/run_ok/closures.expected
```

### run_fail

Place a `.dux` file in `tests/run_fail/`.  Write a program that deliberately
exits non-zero (via a failed assertion, an uncaught exception, or an explicit
exit call).

---

## 4. Runtime C file naming conventions

The runtime lives in `src/runtime/`.  The public surface of the entire
runtime is declared in a single header:

```
src/runtime/duxrt.h    — public API; included by generated LLVM IR via extern declarations
```

Each feature module has its own implementation pair:

```
src/runtime/<module>_rt.c    — implementation
src/runtime/<module>_rt.h    — module-private declarations (included only within the module)
```

Examples from the existing runtime:

| Module | Files |
|---|---|
| String | `str.c` (core), `str_rt.c` / `str_rt.h` (extended) |
| List | `list.c` |
| Dict | `dict.c` |
| Math | `math_rt.c` |
| I/O | `io.c`, `file_rt.c`, `fs_rt.c` |
| Threads | `thread_rt.c` |
| Time | `time_rt.c`, `timefmt_rt.c` |
| Async | `async_rt.c`, `achan_rt.c`, `future_rt.c` |
| System | `sys_rt.c` |
| Network | `net_http.c`, `net_tls.c`, `socket_rt.c`, `ws_rt.c` |

### Naming rules

1. **Public symbols** must be declared in `duxrt.h`.  Each public symbol is
   prefixed with `duxrt_`:

   ```c
   // duxrt.h
   DuxStr duxrt_str_concat(DuxStr a, DuxStr b);
   int64_t duxrt_list_len(DuxList *lst);
   ```

2. **Private helper functions** must be either `static` (file-scope) or
   prefixed with `duxrt_<module>_` to avoid symbol collisions across
   translation units:

   ```c
   // math_rt.c — not exposed in duxrt.h
   static double clamp_to_range(double x, double lo, double hi) { ... }
   static double duxrt_math_do_thing(double x) { ... }
   ```

3. **No global mutable state** unless explicitly marked `_Thread_local`
   (C11) or `thread_local` (C++11).  Any global that must exist must be
   documented with a comment explaining its lifetime and thread-safety
   guarantee.

   Acceptable:
   ```c
   static _Thread_local int duxrt_errno_cache = 0;
   ```

   Not acceptable without documentation:
   ```c
   static int g_counter = 0;   // ← shared mutable state — add a comment
   ```

---

## 5. Adding new stdlib modules

The standard library sources live in `stdlib/` (Dux source files) and
`src/runtime/` (C runtime backing).

### Step-by-step

1. **Create the Dux source file** in the appropriate subdirectory under
   `stdlib/`:

   ```
   stdlib/<module>.dux          — if top-level (e.g. stdlib/math.dux)
   stdlib/<category>/<module>.dux  — if nested (e.g. stdlib/sys/env.dux)
   ```

2. **Create the C runtime backing** (if the module calls native code):

   ```
   src/runtime/<module>_rt.c
   src/runtime/<module>_rt.h    (optional, for internal declarations)
   ```

   Follow the naming rules in §4.

3. **Declare public functions in `duxrt.h`** using the `duxrt_` prefix.

4. **Register the C file in `CMakeLists.txt`** so it is compiled into
   the runtime library.  Find the `target_sources(duxrt ...)` block and
   add your file:

   ```cmake
   target_sources(duxrt PRIVATE
       src/runtime/existing_rt.c
       src/runtime/your_new_module_rt.c   # add here
   )
   ```

5. **Add tests** for the new module:
   - Positive cases in `tests/run_ok/` with matching `.expected` files.
   - Error cases in `tests/run_fail/` if applicable.
   - If the module exposes Dux-level APIs, add `tests/sema_ok/` coverage
     to verify the type-checker accepts correct usage.

6. **Update the stdlib documentation** in `docs/stdlib/` to describe the
   new module's API (functions, types, examples).

### Module naming convention

| Import path | Dux source | C runtime prefix |
|---|---|---|
| `import math` | `stdlib/math.dux` | `duxrt_math_` |
| `import str` | `stdlib/str.dux` | `duxrt_str_` |
| `import io` | `stdlib/io/io.dux` | `duxrt_io_` |
| `import thread` | `stdlib/thread/thread.dux` | `duxrt_thread_` |
| `import sys.env` | `stdlib/sys/env.dux` | `duxrt_env_` |
| `import data.json` | `stdlib/data/json.dux` | `duxrt_json_` |

---

## 6. Code style

- **C++ sources**: 4-space indentation, no tabs.  Follow the conventions
  already present in `src/`.  Run `clang-format` if in doubt.
- **C runtime files**: 4-space indentation, C11 style.
- **Dux source files**: 4-space indentation (enforced by `.editorconfig`).
- **CMake files**: 4-space indentation.
- All text files must end with a single newline (`insert_final_newline = true`
  in `.editorconfig`).

---

## 7. Submitting changes

1. Fork the repository and create a feature branch from `master`.
2. Make your changes and ensure `ctest` passes.
3. Open a pull request describing what was changed and why.
4. A maintainer will review and merge the PR.

For larger changes (new language features, breaking API changes), open an
issue first to discuss the design before writing code.
