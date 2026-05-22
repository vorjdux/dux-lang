# Dux Lang

## Why did we create yet another programming language?

- We love a good challenge
- Why not?
- We've seen plenty of languages drowning in crazy syntax sugar — but really, *do we need all that*?
- Our goal: fuse the elegance of Python with the strength of C++, without going completely insane *(just a little bit 😄)*
- And it **compiles**! Yeahhh \nn/_

---

An experimental compiled programming language that targets native code via LLVM IR.
Statically typed, clean syntax, no runtime surprises.

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
./build/dux examples/euler12.dux -O2 -o /tmp/euler12
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

# ── Type-inferred variables (auto) ───────────────────────────────────────────

int main() {
    auto name  = "Dux"        # str
    auto count = 0            # int
    auto ratio = 1.5          # double
    auto big   = 9000000000l  # long  (l suffix)
    auto temp  = 98.6f        # real  (f suffix, 32-bit float)

    println(name)
    println(count)

    # ── Lambdas ───────────────────────────────────────────────────────────────

    # Style A: return type lives inside the lambda, auto on the left
    auto sq = fn(int n) -> int => n * n
    println(sq(7))   # 49

    # Style B: return type is the variable prefix, lambda body is untyped
    int cube = fn(int n) => n * n * n
    println(cube(3)) # 27

    # Closure — captures outer variable by value
    int base = 10
    auto addBase = fn(int n) -> int => n + base
    println(addBase(5))  # 15

    # Block body (multi-line)
    auto greet = fn(str name) -> void {
        str msg = "hello, " + name
        println(msg)
    }
    greet("world")

    # ── Stdlib ───────────────────────────────────────────────────────────────
    println(math.sqrt(2.0))  # 1.41421...

    return 0
}
```

More examples in [`examples/`](examples/), including the full language showcase in
[`examples/design.dux`](examples/design.dux).

## Variables and type inference

Dux is statically typed. Every variable has a fixed type determined at compile time.
You can either annotate the type explicitly or let the compiler infer it with `auto`:

```dux
# Explicit type
int    age  = 42
long   big  = 9000000000
double pi   = 3.14159
real   temp = 98.6       # 32-bit float
str    name = "Dux"
bool   ok   = true

# Inferred with auto
auto age  = 42
auto big  = 9000000000   # inferred as int — use suffix for long:
auto bigl = 9000000000l  # long
auto pi   = 3.14159
auto name = "Dux"
auto ok   = true
```

## Literal suffixes

Suffix a literal to pin its type when `auto` would otherwise pick the wrong width:

| Suffix | Type     | Example  | Notes                        |
|--------|----------|----------|------------------------------|
| *(none)*| `int`   | `42`     | 32-bit signed integer        |
| `i`    | `int`    | `42i`    | explicit, same as plain `42` |
| `l`    | `long`   | `42l`    | 64-bit signed integer        |
| *(none)*| `double`| `3.14`   | 64-bit float                 |
| `d`    | `double` | `3.14d`  | explicit, or `42d` = 42.0    |
| `f`    | `real`   | `3.14f`  | 32-bit float                 |

```dux
auto a = 42        # int
auto b = 42l       # long
auto c = 3.14      # double
auto d = 3.14f     # real (32-bit)
auto e = 42d       # double from integer literal
auto f = 42f       # real  from integer literal
```

## Lambdas

Lambda expressions are first-class values. Two equivalent declaration styles:

```dux
# Style A — return type inside the lambda, auto on the left
auto sq = fn(int n) -> int => n * n
auto sq = fn(int n) -> int { return n * n }

# Style B — return type as the variable prefix, lambda body is untyped
int sq = fn(int n) => n * n
int sq = fn(int n) { return n * n }
```

Lambdas close over variables in the enclosing scope:

```dux
int offset = 10
auto add = fn(int n) -> int => n + offset
println(add(5))  # 15
```

Higher-order functions declare their parameter type with the full `fn(T) -> R` form:

```dux
int apply(fn(int) -> int f, int x) {
    return f(x)
}

int main() {
    auto double_ = fn(int n) -> int => n * 2
    println(apply(double_, 7))  # 14
    return 0
}
```

## Language features

- **Types**: `int`, `long`, `real`, `double`, `bool`, `str`, `list`, `dict`, `ptr`
- **Type inference**: `auto` for variables; literal suffixes `i` `l` `d` `f` to pin numeric width
- **Classes** with constructors, destructors, field defaults, single inheritance, interfaces
- **Generics**: parametric classes via monomorphisation (`Box<T>`, `Pair<A, B>`)
- **Closures / lambdas**: `auto f = fn(params) -> R => expr` or `R f = fn(params) => expr`; lexical capture
- **Operator overloading**: `operator__add`, `operator__eq`, `operator__lt`, `operator__index`, …
- **Namespaces**: dotted names (`com.example.pkg`), file-based imports, selective imports
- **Control flow**: `if/else`, `while`, `do/while`, `for … in` (range, `range(n)`, C-style), `switch`
- **Labeled breaks/continues**: `&label while …` / `break &label`
- **Exception handling**: `try { … } catch (ExcType e) { … }` / `catch …`; `throw expr`
- **Defer**: LIFO scope-exit cleanup blocks (`defer { … }`)
- **RAII**: destructors called automatically on scope exit
- **Unsafe blocks**: `unsafe { … }` for low-level operations
- **C FFI**: `extern "C" ret name(params)` to call any C function directly
- **Stdlib**: `import math`, `import str`, `import io`
- **Built-ins**: `println`, `print`, `readline`, `range`, `len`, `assert`, `str()`, `int()`, `double()`
- **Optimisation**: `-O0` through `-O3` via LLVM `PassBuilder`
- **Debug info**: `-g` emits DWARF via `DIBuilder`

See [`docs/spec.md`](docs/spec.md) for the full language specification and
[`docs/stdlib/`](docs/stdlib/) for the standard library API reference.

## License

MIT — see [LICENSE](LICENSE)
