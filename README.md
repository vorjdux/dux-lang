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
sudo apt install cmake flex bison g++-13 llvm-18-dev clang-18
```

> `clang-18` is optional but enables **LTO** (link-time optimisation): the compiler
> merges the runtime into your program module before optimisation, allowing LLVM to
> inline runtime helpers end-to-end.  Without it the compiler still works; LTO is
> silently skipped.

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
./build/dux --compile -O2 examples/euler12.dux -o euler12
./euler12        # prints 842161320

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

---

## Language sample

```dux
import math

int main() {
    auto name  = "Dux"        # str
    auto count = 0            # int
    auto ratio = 1.5          # double
    auto big   = 9000000000l  # long  (l suffix)
    auto temp  = 98.6f        # real  (f suffix, 32-bit float)

    println(name)

    # Lambdas
    auto sq = fn(int n) -> int => n * n
    println(sq(7))   # 49

    # Closure — captures outer variable by value
    int base = 10
    auto addBase = fn(int n) -> int => n + base
    println(addBase(5))  # 15

    # Stdlib
    println(math.sqrt(2.0))  # 1.41421...

    return 0
}
```

More examples in [`examples/`](examples/), including the full language showcase in
[`examples/design.dux`](examples/design.dux).

---

## Language features

### Types

| Type | Description |
|------|-------------|
| `int` | 32-bit signed integer |
| `long` | 64-bit signed integer |
| `double` | 64-bit float |
| `real` | 32-bit float |
| `bool` | boolean |
| `str` | reference-counted string (hybrid inline/heap allocation) |
| `list` | dynamic array |
| `dict` | hash map |
| `ptr` | raw pointer (for C FFI / unsafe code) |

### Type inference and literal suffixes

```dux
auto age  = 42           # int
auto bigl = 9000000000l  # long  (l suffix)
auto pi   = 3.14159      # double
auto temp = 98.6f        # real  (f suffix, 32-bit float)
auto name = "Dux"        # str
auto ok   = true         # bool
```

| Suffix | Type | Example |
|--------|------|---------|
| *(none)* | `int` | `42` |
| `l` | `long` | `42l` |
| *(none)* | `double` | `3.14` |
| `f` | `real` | `3.14f` |
| `d` | `double` (from int literal) | `42d` |

### Classes, inheritance and interfaces

```dux
class Animal {
    str name

    Animal(str n) {
        this.name = n
    }

    str speak() {
        return "..."
    }
}

class Dog(Animal) {
    Dog(str n) {
        Animal(n)
    }

    str speak() {
        return "Woof!"
    }
}

int main() {
    Dog d = new Dog("Rex")
    println(d.speak())   # Woof!
    return 0
}
```

Destructors (`~ClassName()`) are called automatically at scope exit (RAII).
Empty destructors are elided at compile time — no overhead for trivial types.

### Generics

Parametric classes via monomorphisation — each instantiation is a separate native type:

```dux
class Box<T> {
    T value
    Box(T v) { this.value = v }
    T unwrap() { return this.value }
}

class Pair<A, B> {
    A first
    B second
    Pair(A a, B b) { this.first = a; this.second = b }
}

int main() {
    Box<int>     bi = new Box<int>(42)
    Box<str>     bs = new Box<str>("hello")
    Pair<int,str> p = new Pair<int,str>(7, "seven")
    println(bi.unwrap())   # 42
    println(bs.unwrap())   # hello
    println(p.first)       # 7
    return 0
}
```

### Closures and lambdas

```dux
# Style A — return type inside the lambda
auto sq = fn(int n) -> int => n * n

# Style B — return type as variable prefix
int cube = fn(int n) => n * n * n

# Block body (multi-line)
auto greet = fn(str name) -> void {
    println("hello, " + name)
}

# Higher-order function
int apply(fn(int) -> int f, int x) {
    return f(x)
}
```

### Operator overloading

```dux
class Vec2 {
    double x
    double y
    Vec2(double x, double y) { this.x = x; this.y = y }

    Vec2 operator__add(Vec2 other) {
        return new Vec2(this.x + other.x, this.y + other.y)
    }

    bool operator__eq(Vec2 other) {
        return this.x == other.x and this.y == other.y
    }
}

int main() {
    Vec2 a = new Vec2(1.0, 2.0)
    Vec2 b = new Vec2(3.0, 4.0)
    Vec2 c = a + b   # calls operator__add
    println(c.x)     # 4.0
    return 0
}
```

Supported operators: `+` `−` `*` `/` `==` `!=` `<` `>` `<=` `>=` `[]`

### Exception handling

```dux
class ValueError(Exception) {
    ValueError(str msg) { Exception(msg) }
}

int parse(str s) {
    if s == "" {
        throw new ValueError("empty input")
    }
    return 42
}

int main() {
    try {
        int v = parse("")
    } catch (ValueError e) {
        println("caught: " + e.message)
    }
    return 0
}
```

### Defer and RAII

```dux
int main() {
    defer { println("cleanup 2") }
    defer { println("cleanup 1") }  # runs first (LIFO)
    println("work")
    return 0
    # prints: work / cleanup 1 / cleanup 2
}
```

Class destructors are called automatically at scope exit:

```dux
class Handle {
    Handle()  { println("open")  }
    ~Handle() { println("close") }  # called when handle goes out of scope
}

int main() {
    Handle h = new Handle()
    println("using")
    return 0
    # prints: open / using / close
}
```

### Namespaces and imports

```dux
# Import a stdlib module
import math
println(math.sqrt(2.0))

# Import specific symbols from a file
import { factorial, fibonacci } from "./algorithms"

# Full file import — all exported names available with module prefix
import "./utils"
utils.helper()

# Package-style namespace declaration
namespace com.example.mylib
```

### Unsafe blocks and C FFI

Call any C function directly with `extern "C"` and access low-level operations inside
`unsafe` blocks:

```dux
extern "C" ptr malloc(long size)
extern "C" void free(ptr p)

int main() {
    ptr buf = null
    unsafe {
        buf = malloc(1024l)
    }
    # ... use buf ...
    unsafe { free(buf) }
    return 0
}
```

### StringBuilder

Efficient mutable string accumulation backed by a pre-allocated contiguous buffer —
O(1) amortised append, single allocation at build time:

```dux
import string_builder

int main() {
    StringBuilder sb = new StringBuilder()
    sb.append("hello")
    sb.append(", ")
    sb.append("world")
    str result = sb.build()
    println(result)   # hello, world
    return 0
}
```

### Standard library

| Module | Contents |
|--------|----------|
| `math` | `sqrt`, `pow`, `floor`, `ceil`, `abs`, `min`, `max`, `log`, `log2`, `sin`, `cos` |
| `str` | `len`, `slice`, `index`, `eq`, `from_int`, `from_double` |
| `io` | `println`, `print`, `readline` |
| `string_builder` | `StringBuilder` class — efficient string accumulation |

---

## Performance

Dux compiles to native code through LLVM and matches C performance on most workloads.
At `-O2`, the compiler enables **LTO**: the runtime library is merged into the
program module as LLVM bitcode before optimisation, so the inliner can eliminate
call overhead across the translation-unit boundary — the same advantage that
C++ gets from header-only implementation.

| | math loop | fib(35) | string build | alloc 1M |
|--|:---------:|:-------:|:------------:|:--------:|
| **C** (gcc -O2) | 2 ms | 21 ms | 3 ms | 3 ms |
| **C++** (g++ -O2) | 2 ms | 21 ms | 3 ms | 3 ms |
| **Go** | 39 ms | 56 ms | 3 ms | 3 ms |
| **Dux** (-O2 + LTO) | **3 ms** | **31 ms** | **3 ms** | **2 ms** |
| Node.js 22 | 92 ms | 134 ms | 37 ms | 44 ms |
| Python 3.11 | 4 110 ms | 1 180 ms | 14 ms | 225 ms |

*4-core Intel Xeon @ 2.80 GHz, Linux 6.18 (x86-64). Best of 3 runs.*

→ **[Full benchmark analysis with methodology and per-fix breakdown](benchmarks/README.md)**

---

## Variables and type inference

Dux is statically typed. Every variable has a fixed type determined at compile time.
You can either annotate the type explicitly or let the compiler infer it with `auto`:

```dux
# Explicit type
int    age  = 42
long   big  = 9000000000l
double pi   = 3.14159
real   temp = 98.6f
str    name = "Dux"
bool   ok   = true

# Inferred with auto
auto age  = 42
auto pi   = 3.14159
auto name = "Dux"
```

## Compiler flags

| Flag | Effect |
|------|--------|
| `--compile` | Compile to native executable |
| `--emit-ir` | Dump LLVM IR (useful for debugging codegen) |
| `--emit-obj` | Emit object file only |
| `--check` | Semantic analysis only, no codegen |
| `-O0` … `-O3` | Optimisation level (LTO kicks in at `-O1`+) |
| `-g` | Emit DWARF debug information |
| `--dump-ast` | Print the parsed AST |

See [`docs/spec.md`](docs/spec.md) for the full language specification and
[`docs/stdlib/`](docs/stdlib/) for the standard library API reference.

## License

MIT — see [LICENSE](LICENSE)
