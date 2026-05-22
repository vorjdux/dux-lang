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

class Stack<T> {
    list data

    Stack() {
        this.data = []
    }

    void push(T v) {
        this.data += [v]
    }

    T pop() {
        int last = len(this.data) - 1
        T v = this.data[last]
        return v
    }
}

int main() {
    Stack<int> s = new Stack<int>()
    s.push(1)
    s.push(2)
    s.push(3)
    println(s.pop())    # 3

    fn(int) -> int sq = fn(int n) => n * n
    println(sq(7))      # 49

    double r = math.sqrt(2.0)
    println(r)          # 1.4142...

    return 0
}
```

More examples in [`examples/`](examples/), including the full language showcase in
[`examples/design.dux`](examples/design.dux).

## Language features

- **Types**: `int`, `long`, `real`, `double`, `bool`, `str`, `list`, `dict`, `tuple`, `ptr`
- **Classes** with constructors, destructors, field defaults, single/multiple inheritance, interfaces
- **Generics**: parametric classes and functions via monomorphisation (`Box<T>`, `Pair<A, B>`)
- **Closures / lambdas**: first-class `fn(T) -> R` function types with lexical capture
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
