# Dux Lang

An experimental compiled programming language that fuses Python's elegance with C++'s strength — without the crazy syntax.

## Goals

- Clean, readable syntax with optional semicolons (Go-style automatic insertion)
- Static typing with type inference
- Classes, interfaces, decorators, destructors, and constructor init-lists
- Compiled via a hand-written Flex/Bison front-end targeting C++ back-end

## Requirements

| Tool | Version |
|------|---------|
| CMake | ≥ 3.25 |
| C++ compiler | C++23 (GCC 13+ or Clang 16+) |
| Flex | ≥ 2.6 |
| Bison | ≥ 3.8 |

```bash
# Ubuntu / Debian
sudo apt install cmake flex bison g++-13
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

```bash
# Parse a file and dump the AST
./build/dux --dump-ast examples/design.dux

# Parse from stdin
echo "void main() { println('hi') }" | ./build/dux --dump-ast

# Debug flags
./build/dux --trace-lex   examples/hello_world.dux   # flex token trace
./build/dux --trace-parse examples/hello_world.dux   # bison state trace
```

## Run tests

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

## Language sample

```dux
namespace com.example {

    import dux.datetime.date

    class Person(public object) {

        public:

        Person(str name, int age) {
            this._name = name
            this._age  = age
        }

        ~Person() {
            delete this._hand
        }

        int burn_year() {
            return date.today().year - this._age
        }

        private:
        str _name
        int _age
    }

    void main() {
        Person p = new Person('Ada', 36)
        println(p.burn_year())

        for int i in 0..<10 {
            println(i)
        }
    }
}
```

## Project layout

```
src/
  lexer/dux.l        — Flex lexer (ASI, string literals, range ops)
  parser/dux.y       — Bison 3.8 LALR(1) grammar
  ast/ast.hpp        — 35+ AST node types + Visitor interface
  ast/ast.cpp        — accept() implementations
  ast/printer.hpp    — AST pretty-printer
  driver/driver.hpp  — Driver class (orchestrates parse)
  driver/driver.cpp
  main.cpp           — CLI entry point
examples/
  design.dux         — Full language showcase
  hello_world.dux
  euler12.dux
CMakeLists.txt
```

## License

MIT — see [LICENSE](LICENSE)
