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

void main() {
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
}
```

More examples in [`examples/`](examples/), including the full language showcase in
[`examples/design.dux`](examples/design.dux).

---

## Language features

> **Automatic semicolon insertion:** Dux uses a Go-style rule — the lexer inserts
> a semicolon at a newline whenever the preceding token can end a statement
> (identifier, literal, `)`, `]`, `}`, `true`, `false`, etc.).  This means
> a closing `}` should generally be on its own line, and when you need a
> one-liner block body you must add an explicit `;` before the `}`.
> All examples in this document follow these rules.

### Types

| Type | Description |
|------|-------------|
| `int` | 32-bit signed integer |
| `long` | 64-bit signed integer |
| `double` | 64-bit float |
| `real` | 32-bit float |
| `bool` | boolean (`true` / `false`) |
| `str` | reference-counted string (hybrid inline/heap allocation) |
| `list` | dynamic array |
| `dict` | hash map |
| `ptr` | raw pointer (for C FFI / unsafe code) |
| `object` | base type for heap-allocated class instances |
| `void` | no value (function return) |
| `auto` | compiler-inferred type |

### Type inference and literal suffixes

```dux
# Explicit types
int    age  = 42
long   big  = 9000000000l
double pi   = 3.14159
real   temp = 98.6f
str    name = "Dux"
bool   ok   = true

# Inferred with auto
auto age  = 42           # int
auto big  = 9000000000l  # long  (l suffix)
auto pi   = 3.14159      # double
auto temp = 98.6f        # real  (f suffix)
auto name = "Dux"        # str
auto ok   = true         # bool
```

| Suffix | Type | Example |
|--------|------|---------|
| *(none)* | `int` | `42` |
| `l` | `long` | `42l` |
| *(none)* | `double` | `3.14` |
| `f` | `real` | `3.14f` |
| `d` | `double` (from integer literal) | `42d` |

### Comments and doc-strings

```dux
# Single-line comment

/*
   Multi-line
   comment
*/

"""
  Triple-quoted doc-string.
  Typically used at the top of a file or function.
"""
```

### Variables and constants

```dux
int  x = 10
str  s = "hello"
bool flag = false

# Constant — value fixed at compile time, no mutation allowed
const int MAX = 100

# Static local — retains value between calls
int counter() {
    static int n = 0
    n += 1
    return n
}
```

### Operators

```dux
# Arithmetic
int a = 10 + 3   # 13
int b = 10 - 3   # 7
int c = 10 * 3   # 30
int d = 10 / 3   # 3  (integer division)
int e = 10 % 3   # 1

# Compound assignment
a += 5
a -= 2
a *= 3
a /= 2
a %= 4

# Increment / decrement (statement form)
a++
a--

# Comparison
bool lt = a < b
bool gt = a > b
bool eq = a == b
bool ne = a != b
bool le = a <= b
bool ge = a >= b

# Logical
bool t = true && false   # and
bool u = true || false   # or
bool v = !true           # not
bool w = true and false  # keyword form
bool z = true or false   # keyword form
```

### Control flow

#### if / else if / else

```dux
int x = 5
if x > 10 {
    println("big")
} else if x > 3 {
    println("medium")
} else {
    println("small")
}
```

#### while

```dux
int i = 0
while i < 5 {
    println(i)
    i += 1
}
```

#### do-while

```dux
int x = 0
do {
    x++
} while x < 3
println(x)   # 3
```

#### for — range forms

```dux
# Inclusive range  (1, 2, 3, 4, 5)
for int i in 1..=5 {
    println(i)
}

# Exclusive range  (0, 1, 2)
for int i in 0..<3 {
    println(i)
}

# range(n) — equivalent to 0..<n
for int i in range(3) {
    println(i)
}
```

#### for — C-style

```dux
# for init, cond, step
for int i = 0, i < 5, i++ {
    println(i)
}
```

#### switch

```dux
int x = 3
switch x {
    case 1:
        println("one")
        break
    case 2:
        println("two")
        break
    case 3:
        println("three")
        break
    default:
        println("other")
}
```

#### match

Pattern matching is exhaustive. Use `_` as the wildcard arm.
Each arm body must use a block `{ }` with the statement on its own line
(Dux's automatic semicolon insertion requires a line break before the closing `}`).

```dux
# Match on integer
int n = 2
match n {
    1 => {
        println("one")
    }
    2 => {
        println("two")
    }
    _ => {
        println("other")
    }
}

# Match on string
str word = "apple"
match word {
    "apple" => {
        println("fruit")
    }
    "carrot" => {
        println("vegetable")
    }
    _ => {
        println("unknown")
    }
}

# Match on bool
bool flag = true
match flag {
    true => {
        println("yes")
    }
    false => {
        println("no")
    }
}

# Match on enum variant
enum Dir {
    North,
    South,
    East,
    West,
}

Dir d = Dir.East
match d {
    Dir.North => {
        println("N")
    }
    Dir.South => {
        println("S")
    }
    Dir.East => {
        println("E")
    }
    _ => {
        println("W")
    }
}
```

#### Labeled break

Break out of an outer loop by name:

```dux
&outer while true {
    int k = 0
    while k < 5 {
        if k == 2 {
            break &outer
        }
        k++
    }
}
println("after break")
```

#### break and continue

```dux
for int i in range(10) {
    if i == 3 { continue }
    if i == 7 { break }
    println(i)
}
```

### Functions

```dux
# Regular function
int add(int a, int b) {
    return a + b
}

# Void function
void greet(str name) {
    println("Hello, " + name + "!")
}

# Generic function — monomorphised per call site
int identity<T>(T x) {
    return x
}

void main() {
    println(add(3, 4))        # 7
    greet("Dux")              # Hello, Dux!
    println(identity<int>(42)) # 42
}
```

### Closures and lambdas

```dux
# Arrow style — return type inside
auto sq = fn(int n) -> int => n * n

# Block body — multi-statement
auto greet = fn(str name) -> void {
    println("hello, " + name)
}

# Closure — captures variables from the enclosing scope by value
int base = 10
auto addBase = fn(int x) -> int => x + base
println(addBase(5))   # 15

# Higher-order function — fn type as parameter
int apply(fn(int) -> int f, int x) {
    return f(x)
}
println(apply(sq, 6))   # 36
```

### Enums

```dux
enum Color {
    Red,
    Green,
    Blue
}

void main() {
    int r = Color.Red     # 0
    int g = Color.Green   # 1
    int b = Color.Blue    # 2
    println(r)
    println(g)
    println(b)
}
```

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

# Single inheritance — class Child(Parent)
class Dog(Animal) {
    Dog(str n) : Animal(n) {
    }

    str speak() {
        return "Woof!"
    }
}

void main() {
    Dog d = new Dog("Rex")
    println(d.speak())   # Woof!
    println(d.name)      # Rex
}
```

Destructors (`~ClassName()`) are called automatically at scope exit (RAII).
Empty destructors are elided at compile time — no overhead for trivial types.

```dux
class Handle {
    Handle()  { println("open");  }
    ~Handle() { println("close"); }
}

void main() {
    Handle h = new Handle()
    println("using")
    # prints: open / using / close
}
```

#### Access modifiers

```dux
class Point {
    public:
    int x
    int y

    private:
    int _cache

    public:
    Point(int x, int y) {
        this.x = x
        this.y = y
        this._cache = 0
    }
}
```

#### Static fields and methods

Static members belong to the class, not instances. Static local variables inside
functions retain their value between calls.

```dux
class Counter {
    static int count = 0

    static void increment() {
        Counter.count += 1
    }

    static int value() {
        return Counter.count
    }
}

void main() {
    Counter.increment()
    Counter.increment()
    Counter.increment()
    println(Counter.value())   # 3
}

# Static local variable in a function
int next_id() {
    static int id = 0
    id += 1
    return id
}
```

#### Property getters and setters

Methods declared with `-> get` / `-> set` become property accessors:

```dux
class Circle {
    double _radius

    Circle(double r) { this._radius = r; }

    double radius -> get { return this._radius; }
    void   radius -> set { this._radius = value; }

    double area -> get {
        return 3.14159 * this._radius * this._radius
    }
}
```

### Generics

Parametric classes and functions via monomorphisation — each instantiation is a
separate native type with zero virtual-dispatch overhead:

```dux
class Box<T> {
    T value
    Box(T v) { this.value = v; }
    T unwrap() { return this.value; }
}

class Pair<A, B> {
    A first
    B second
    Pair(A a, B b) { this.first = a; this.second = b; }
}

void main() {
    Box<int>      bi = new Box<int>(42)
    Box<str>      bs = new Box<str>("hello")
    Pair<int,str> p  = new Pair<int,str>(7, "seven")
    println(bi.unwrap())   # 42
    println(bs.unwrap())   # hello
    println(p.first)       # 7
}
```

### Operator overloading

```dux
class Vec2 {
    int x
    int y

    Vec2(int x, int y) { this.x = x; this.y = y; }

    Vec2 operator__add(Vec2 other) {
        return new Vec2(this.x + other.x, this.y + other.y)
    }

    bool operator__eq(Vec2 other) {
        return this.x == other.x && this.y == other.y
    }

    bool operator__lt(Vec2 other) {
        return this.x < other.x
    }

    int operator__index(int i) {
        if i == 0 { return this.x; }
        return this.y
    }
}

void main() {
    Vec2 a = new Vec2(1, 2)
    Vec2 b = new Vec2(3, 4)
    Vec2 c = a + b        # operator__add  → Vec2(4, 6)
    println(c.x)          # 4
    if a < b { println("lt"); }   # operator__lt
    println(a[0])         # operator__index → 1
}
```

Supported operators: `+` `−` `*` `/` `%` `==` `!=` `<` `>` `<=` `>=` `[]`

### Exception handling

Any class instance can be thrown. Use `catch ...` to catch everything, or
`catch (TypeName varName)` to bind the caught object to a variable:

```dux
class AppError {
    str message
    AppError(str msg) {
        this.message = msg
    }
}

int parse(str s) {
    if s == "" {
        throw new AppError("empty input")
    }
    return 42
}

void main() {
    # Catch-all — matches any thrown value
    try {
        int v = parse("")
    } catch ... {
        println("caught an error")
    }

    # Bind caught object to a variable
    try {
        throw new AppError("oops")
    } catch (AppError e) {
        println("caught: " + e.message)
    }
}
```

### Defer and RAII

```dux
void main() {
    defer { println("cleanup 2"); }
    defer { println("cleanup 1"); }  # runs first (LIFO)
    println("work")
    # prints: work / cleanup 1 / cleanup 2
}
```

Class destructors fire automatically when the object goes out of scope:

```dux
class Logger {
    str tag
    Logger(str t)  { println("open:"  + t); this.tag = t; }
    ~Logger()      { println("close:" + this.tag); }
}

void example() {
    Logger a = new Logger("A")
    defer { println("deferred"); }
    Logger b = new Logger("B")
    println("body")
    # prints: open:A / open:B / body / close:B / deferred / close:A
    # (LIFO cleanup stack: b's dtor, then defer, then a's dtor)
}
```

### Assertions

```dux
assert(2 + 2 == 4)        # passes
assert(x > 0)             # aborts with message if x ≤ 0
```

### Namespaces and imports

```dux
# Import a stdlib module
import math
println(math.sqrt(2.0))

# Import specific symbols — available without prefix
import { factorial, fibonacci } from "./algorithms"

# Full file import — names available under module prefix
import "./utils"
utils.helper()

# Standard library modules
import str
import thread
import thread.chan
import thread.pool
import sys.sys
import sys.env

# Package-style namespace declaration
namespace com.example.mylib
```

### Unsafe blocks and C FFI

Call any C function directly with `extern "C"` and perform low-level operations
inside `unsafe` blocks:

```dux
extern "C" ptr malloc(long size)
extern "C" void free(ptr p)
extern "C" double duxrt_math_sqrt(double x)

double fast_sqrt(double x) {
    double result
    unsafe {
        result = duxrt_math_sqrt(x)
    }
    return result
}

void main() {
    ptr buf = null
    unsafe { buf = malloc(1024l) }
    # ... use buf ...
    unsafe { free(buf) }
    println(fast_sqrt(9.0))   # 3.0
}
```

### Standard library

#### `math`

```dux
import math

println(math.sqrt(2.0))        # 1.41421...
println(math.pow(2.0, 10.0))   # 1024.0
println(math.floor(3.7))       # 3.0
println(math.ceil(3.2))        # 4.0
println(math.abs(-5.0))        # 5.0
println(math.log(2.718281))    # ~1.0
println(math.log2(8.0))        # 3.0
println(math.sin(0.0))         # 0.0
println(math.cos(0.0))         # 1.0
println(math.min(2.0, 3.0))    # 2.0
println(math.max(2.0, 3.0))    # 3.0
# Integer variants
println(math.abs_i(-7l))       # 7
println(math.min_i(3l, 5l))    # 3
println(math.max_i(3l, 5l))    # 5
```

#### `str`

```dux
import str

str a = "hello"
str b = "world"

println(str.length(a))               # 5
println(str.concat(a, b))            # helloworld
println(str.slice(a, 1l, 4l))        # ell
println(str.index(a, 0l))            # h
println(str.eq(a, b))                # false
println(str.from_int(42l))           # 42
println(str.from_double(3.14))       # 3.14
println(str.to_upper(a))             # HELLO
println(str.to_lower("WORLD"))       # world
println(str.trim("  hi  "))          # hi
println(str.contains(a, "ell"))      # true
println(str.starts_with(a, "hel"))   # true
println(str.ends_with(a, "llo"))     # true
println(str.find(a, "ll"))           # 2
println(str.replace(a, "l", "r"))    # herlo
println(str.replace_all(a, "l", "r")) # herro
println(str.repeat(a, 2l))           # hellohello
println(str.ord("A"))                # 65
println(str.chr(65l))                # A
```

#### `io`

```dux
import io

io.println("hello")    # with newline
io.print("hello")      # no newline
str line = io.readline()
```

The built-in `println` and `print` functions are always available without import.

#### `thread`

```dux
import thread

Mutex m = new Mutex()
m.lock()
m.unlock()
bool ok = m.try_lock()
if ok { m.unlock() }

RWLock rw = new RWLock()
rw.read_lock()
rw.unlock()
rw.write_lock()
rw.unlock()

CondVar cv = new CondVar()
cv.signal()
cv.broadcast()

Once o = new Once()   # run-once guard

long tid = thread.id()
thread.sleep_ms(100l)
```

#### `thread.chan` and `thread.pool`

```dux
import thread.chan

Chan ch = new Chan(0)
long n = ch.pending()
bool closed = ch.is_closed()
int val = ch.try_recv()
ch.close()
```

```dux
import thread.pool

ThreadPool p = new ThreadPool(4)
long workers = p.size()
p.wait()
```

#### `sys.sys` and `sys.env`

```dux
import sys.sys
import sys.env

long pid  = sys.pid()
long ppid = sys.ppid()
str  host = sys.hostname()
```

#### `string_builder`

Efficient mutable string accumulation backed by a pre-allocated contiguous buffer —
O(1) amortised append, single allocation at build time:

```dux
import string_builder

StringBuilder sb = new StringBuilder()
sb.append("hello")
sb.append(", ")
sb.append("world")
str result = sb.build()
println(result)   # hello, world
```

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

---

## Bootstrap test suite (dogfood demos)

The [`bootstrap/`](bootstrap/) directory contains two Dux programs written in Dux
itself.  They are **test programs**, not infrastructure — both `dux_stage2` and
`dux_stage3` are test suites that exercise the language and verify the compiler's
output, nothing more.

```
bootstrap/
  dux_stage2.dux   — a Dux-written lexer + re-printer for Dux source (~2 400 lines)
  dux_stage3.dux   — a round-trip test suite that validates stage2 output
```

**The real implementations live in `src/`:**

| Concern | Location |
|---------|----------|
| Lexer | `src/lexer/dux.l` (Flex) |
| Parser | `src/parser/dux.y` (Bison) |
| AST printer | `src/ast/printer.hpp` / `src/ast/ast.hpp` |
| Semantic analysis | `src/sema/` |
| Code generation | `src/codegen/codegen.cpp` (LLVM IR) |

`dux_stage2` does not replace any of those.  It is a standalone Dux program that
re-implements a minimal hand-written lexer and a recursive-descent re-printer —
written purely to demonstrate that Dux can express non-trivial programs.  It has
no codegen, no semantic analysis, and no optimiser.

`dux_stage3` drives the verification: it writes small Dux snippets to `/tmp`,
runs `dux_stage2 --dump-ast` on each, feeds the output back to the **real** C++
compiler, executes the resulting binary, and compares stdout to the expected value.

**Neither file is part of the build.**  Building the project always uses the C++
toolchain.  The binaries in `bootstrap/dux_stage2` and `bootstrap/dux_stage3` are
pre-compiled by running the main compiler manually:

```bash
./build/dux --compile bootstrap/dux_stage2.dux -o bootstrap/dux_stage2
./build/dux --compile bootstrap/dux_stage3.dux -o bootstrap/dux_stage3
```

**Running the round-trip suite:**

```bash
./bootstrap/dux_stage3
# dux stage3 — round-trip test suite
# =====================================
# [PASS] hello_world
# [PASS] string_match
# ...
# Results: 13 passed, 0 failed
# OVERALL: PASS
```

**Why it matters:**

Writing a ~2 400-line program in Dux — one that uses classes, generics, closures,
match statements, string operations, process I/O, and a full hand-written lexer —
and having that program compile and produce correct output is the strongest
evidence that the language is consistent and usable.  It is a real dogfood test,
not a toy demo.

---

## License

MIT — see [LICENSE](LICENSE)
