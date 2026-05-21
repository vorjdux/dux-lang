# Dux Lang

An experimental compiled programming language that fuses Python's elegance with C++'s
strength — statically typed, compiles to native code via LLVM IR.

## Status

**M6 — Complete** · 80/80 tests passing · `examples/design.dux` and `examples/euler12.dux`
compile and run end-to-end.

| Milestone | Feature |
|-----------|---------|
| M1 | Flex/Bison lexer + parser |
| M2 | Semantic analysis, type system, symbol table |
| M3 | LLVM IR code generation |
| M4 | Runtime library (duxrt) + native compilation |
| M5 | Stdlib imports (`math`), optimisation pipeline, DWARF debug info |
| M6 | Integration tests, language spec, stdlib docs |

## Requirements

| Tool | Version |
|------|---------|
| CMake | ≥ 3.25 |
| C++ compiler | C++23 (GCC 13+ or Clang 16+) |
| Flex | ≥ 2.6 |
| Bison | ≥ 3.8 |
| LLVM | 18 |

```bash
# Ubuntu / Debian
sudo apt install cmake flex bison g++-13 llvm-18-dev
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Debug build includes AddressSanitizer and UBSan:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

## Usage

```
Usage: ./build/dux [options] [file]

Options:
  --dump-ast      Print the parsed AST to stdout
  --check         Run semantic analysis and report errors
  --emit-ir       Emit LLVM IR (to -o path or stdout)
  --emit-obj      Emit native object file (requires -o path)
  --compile       Compile to a runnable native executable
  -o <path>       Output path
  -O0/-O1/-O2/-O3 Optimisation level (default -O0)
  -g              Emit DWARF debug information
```

```bash
# Compile and run a program
./build/dux --compile examples/euler12.dux -O2 -o /tmp/euler12
/tmp/euler12        # prints 842161320

# Dump AST
./build/dux --dump-ast examples/design.dux

# Emit LLVM IR
./build/dux --emit-ir examples/euler12.dux

# Check for semantic errors only
./build/dux --check examples/design.dux
```

## Run tests

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

## Language sample

```dux
import math

int factorCount(long n) {
    double square = math.sqrt(n)
    int isquare = square
    int count = 0
    for long candidate in 1..=isquare {
        if 0 == n % candidate {
            if candidate * candidate == n {
                count++
            } else {
                count += 2
            }
        }
    }
    return count
}

void main() {
    long triangle = 1
    int index = 1
    while factorCount(triangle) < 1001 {
        index++
        triangle += index
    }
    println(triangle)   # 842161320
}
```

More examples in [`examples/`](examples/), including the full language showcase in
[`examples/design.dux`](examples/design.dux).

## Language features

- **Types**: `int`, `long`, `real`, `double`, `bool`, `str`, `list`, `dict`, `tuple`
- **Classes** with constructors, destructors, field defaults, inheritance, interfaces
- **Namespaces** (dotted names; entry point auto-detected)
- **Control flow**: `if/else`, `while`, `do/while`, `for … in` (range, `range(n)`, C-style), `switch`
- **Labeled breaks/continues**: `&label while …` / `break &label`
- **Exception handling**: `try { … } catch … { … }`
- **Stdlib**: `import math` exposes `math.sqrt`, `math.sin`, etc. (LLVM intrinsics)
- **Built-ins**: `println`, `range`, `assert`
- **Optimisation**: `-O0` through `-O3` via LLVM `PassBuilder`
- **Debug info**: `-g` emits DWARF via `DIBuilder`

See [`docs/spec.md`](docs/spec.md) for the full language specification and
[`docs/stdlib/math.md`](docs/stdlib/math.md) for the standard library API.

## Project layout

```
src/
  lexer/dux.l         — Flex lexer (ASI, string literals, range ops)
  parser/dux.y        — Bison 3.8 LALR(1) grammar
  ast/ast.hpp         — 40+ AST node types + Visitor interface
  ast/ast.cpp         — accept() implementations
  ast/printer.hpp     — AST pretty-printer
  driver/driver.hpp   — orchestrates parse → sema → codegen
  sema/sema.cpp       — type checker, symbol resolution
  sema/types.cpp      — TypeRegistry, subtyping, coercion rules
  codegen/codegen.cpp — LLVM IR emitter
  main.cpp            — CLI entry point
runtime/
  duxrt.c             — runtime library (println, list, dict, math shims)
examples/
  design.dux          — full language feature showcase
  euler12.dux         — Project Euler #12 benchmark (842161320)
tests/
  run_ok/             — compile-and-run tests with golden output
  sema_ok/            — sema tests expected to pass
  sema_fail/          — sema tests expected to fail with errors
  parse_ok/           — parser tests expected to pass
  parse_fail/         — parser tests expected to fail
  codegen_ok/         — IR emission tests
docs/
  spec.md             — language specification v0.1
  stdlib/math.md      — math module API reference
CMakeLists.txt
```

## License

MIT — see [LICENSE](LICENSE)
