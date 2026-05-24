# Dux Language Specification

Version 0.2 — May 2026

---

## 1. Overview

Dux is a statically-typed, compiled programming language with a syntax influenced
by Python and Swift. It compiles to native machine code via LLVM IR. The primary
design goals are readability, safety, and performance.

---

## 2. Lexical Structure

### 2.1 Comments

Single-line comments begin with `#`:
```
# this is a comment
```

Multi-line comments are enclosed in triple quotes:
```
"""
multi-line
comment
"""
```

### 2.2 Identifiers

Identifiers start with a letter or underscore, followed by letters, digits, or
underscores. Identifiers are case-sensitive.

### 2.3 Keywords

```
and       as        assert    async     auto      await
bool      break     case      catch     class     const
continue  default   defer     delete    dict      do
double    else      enum      extern    false     fn
for       from      if        import    in        interface
list      long      match     namespace new       not
null      object    or        private   protected public
ptr       real      return    static    str       super
switch    this      thread_local throw   true      try
tuple     unsafe    void      while
```

### 2.4 Literals

| Kind         | Example                     |
|--------------|-----------------------------|
| Integer      | `42`, `-7`                  |
| Long         | `42l`, `9000000000l`        |
| Float        | `3.14`, `-0.5`              |
| Real (f32)   | `3.14f`, `42f`              |
| String       | `"hello"`, `"world"`        |
| F-string     | `f"Hello {name}!"`          |
| Boolean      | `true`, `false`             |
| Null         | `null`                      |

String literals support standard C escape sequences (`\n`, `\t`, `\\`, `\"`, etc.).

**F-strings** (string interpolation) allow embedding arbitrary expressions directly
in a string literal.  Expressions are enclosed in `{` `}`:

```dux
str name = "World"
int n = 42
println(f"Hello {name}! n={n}")        # Hello World! n=42
println(f"n squared = {n * n}")        # n squared = 1764
println(f"big = {n > 100}")            # big = false
```

Escape a literal `{` with `\{`.  The result of each interpolated expression is
converted to a string using the same rules as the built-in `println`:

| Expression type | Converted to |
|---|---|
| `str` | the string itself |
| `int` / `long` | decimal integer |
| `double` / `real` | decimal float |
| `bool` | `"true"` or `"false"` |

---

## 3. Types

### 3.1 Primitive Types

| Type     | Description                        | LLVM type |
|----------|------------------------------------|-----------|
| `bool`   | Boolean (true/false)               | `i1`      |
| `int`    | 32-bit signed integer              | `i32`     |
| `long`   | 64-bit signed integer              | `i64`     |
| `real`   | 32-bit floating point              | `float`   |
| `double` | 64-bit floating point              | `double`  |
| `str`    | Reference-counted UTF-8 string     | `ptr`     |
| `void`   | No value (function return only)    | `void`    |

### 3.2 Composite Types

| Type     | Description                                                  |
|----------|--------------------------------------------------------------|
| `list`   | Reference-counted dynamic array; stores any element type     |
| `dict`   | Reference-counted hash map; string keys, any-type values     |
| `tuple`  | Fixed-size heterogeneous sequence                            |
| `object` | Base type of all classes                                     |

**`list` and `dict` element storage:**
Elements are stored as `void*` values in the runtime structures.  The compiler
automatically boxes primitive values (packing `int`/`bool`/`double`/`float` bits
directly into the pointer word) when inserting, and unboxes them when reading into
a typed variable.  Pointer types (`str`, class instances, lists, dicts) are stored
and retrieved unchanged.

```dux
list nums = [10, 20, 30]
int a = nums[0]        # unboxed to int: 10
nums[1] = 99           # boxed and stored

dict d = {"x": 42, "y": 7}
int x = d["x"]         # unboxed to int: 42
d["z"] = 100           # boxed and stored
```

### 3.3 Numeric Coercions

Numeric types coerce implicitly in assignments, function calls, and binary
operations. The compiler widens narrower types to the wider type before
performing operations:

- `int` → `long` → `real` → `double`
- Integer promotion applies in binary expressions with mixed widths.

### 3.4 User-Defined Types

Classes and interfaces are user-defined reference types. Object identity is
pointer-based; all class instances are heap-allocated.

---

## 4. Variables

Variables are declared with a type annotation followed by the name:

```
int count = 0
str name = "Alice"
double pi = 3.14159
auto inferred = 42    # type inferred as int
```

`const` declares an immutable binding:

```
const int MAX = 100
```

`static` retains the variable value between function calls:

```
static int counter = 0
```

`thread_local` gives each thread its own copy:

```
thread_local int tid = 0
```

Multiple declarations must be written separately (one per line).

---

## 5. Expressions

### 5.1 Arithmetic Operators

`+` `-` `*` `/` `%`

### 5.2 Comparison Operators

`==` `!=` `<` `<=` `>` `>=`

### 5.3 Logical Operators

`and` `or` `not` (also `&&` `||` `!`)

### 5.4 Bitwise Operators

`&` `|` `^` `~` `<<` `>>`

### 5.5 Assignment Operators

`=` `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`

### 5.6 Increment / Decrement

`x++` `x--` (post-fix only)

### 5.7 Range Expressions

| Syntax     | Meaning                    |
|------------|----------------------------|
| `lo..hi`   | Exclusive: `[lo, hi)`      |
| `lo..<hi`  | Exclusive: `[lo, hi)`      |
| `lo..=hi`  | Inclusive: `[lo, hi]`      |

Ranges are used in `for … in` loops.

### 5.8 Member Access

`object.field` and `object.method(args)`

### 5.9 Index Access and Assignment

```dux
# Read
int v = list[index]       # unboxed to the declared type
str s = dict["key"]       # unboxed to the declared type

# Write
list[index] = expr        # expr is boxed and stored
dict["key"] = expr        # expr is boxed and stored
```

Index reads return the raw element unboxed to whatever type the receiving variable
is declared as.  Index writes box the right-hand side.

### 5.10 Allocation / Deallocation

```
Person p = new Person("Alice", 30)
delete p    # frees class instance; safe no-op if already freed
```

`delete` on a `list` or `dict` releases the reference early; RAII at scope exit
is a no-op (the slot is nulled).

---

## 6. Statements

### 6.1 If / Else

```
if condition {
    ...
} else if other {
    ...
} else {
    ...
}
```

### 6.2 While Loop

```
while condition {
    ...
}
```

### 6.3 Do-While Loop

```
do {
    ...
} while condition
```

### 6.4 For-In Loop

Iterating a range:
```dux
for int i in 0..<10 {
    ...
}
```

Iterating with `range()`:
```dux
for int i in range(10) {
    ...
}
```

Iterating a list:
```dux
list nums = [1, 2, 3]
for int v in nums {
    println(v)
}
```

C-style for loop:
```dux
for int i = 0, i < 10, i++ {
    ...
}
```

### 6.5 Switch Statement

```
switch value {
    case 1:
        println("one")
        break
    case 2:
        println("two")
        break
    default:
        println("other")
}
```

### 6.6 Labeled Loops and Break

A loop can be labeled with `&label`:

```
&outer while true {
    while true {
        break &outer   # breaks the outer loop
    }
}
```

### 6.7 Try / Catch

```
try {
    ...
} catch ... {
    println("error")
}
```

The `...` (ellipsis) in `catch` is a catch-all handler.

### 6.8 Return

```
return expression
return          # in void functions
```

### 6.9 Assert

```
assert(condition)
```

Terminates the program if `condition` is false.

---

## 7. Functions

```
int add(int a, int b) {
    return a + b
}

void greet(str name) {
    println("Hello, " + name)
}
```

Functions must be declared before use (forward declarations are handled
automatically by the compiler within a translation unit).

### 7.1 Async Functions

```dux
async void fetch(str url) {
    str body = await http_get(url)
    println(body)
}
```

Async functions return a `DuxFuture*` internally; `await` blocks until the result
is available.

---

## 8. Classes

```
class Point {
    int x = 0
    int y = 0

    public:

    Point(int x, int y) {
        this.x = x
        this.y = y
    }

    int distanceSq() {
        return this.x * this.x + this.y * this.y
    }
}
```

### 8.1 Access Modifiers

- `public:` — accessible from anywhere
- `private:` — accessible only within the class
- `protected:` — accessible within the class and subclasses

### 8.2 Constructors and Destructors

A constructor has the same name as the class. A destructor is prefixed with `~`:

```
class Resource {
    Resource() { ... }
    ~Resource() { ... }
}
```

Destructors are called automatically at scope exit (RAII).  The compiler inserts
destructor calls in reverse declaration order (last declared, first destroyed).

### 8.3 Inheritance

```
class Animal {
    ...
}

class Dog(Animal) {
    ...
}
```

Multiple base classes are comma-separated. `object` is the implicit root class.

### 8.4 Interfaces

```
interface IShape {
    public:
    double area()
    double perimeter()
}

class Circle(IShape) {
    ...
}
```

### 8.5 Field Default Values

Fields may have default values applied before any constructor runs:

```
class Counter {
    int value = 0   # initialized to 0 on each new Counter()
}
```

---

## 9. Namespaces

```
namespace com.example.myapp {
    void main() {
        println("Hello")
    }
}
```

The compiler automatically wraps a namespace-qualified `main()` as the program
entry point.

---

## 10. Imports

```
import math
```

Imports a standard library module. Available stdlib modules:
`math`, `str`, `io`, `thread`, `thread.chan`, `thread.pool`, `sys.sys`, `sys.env`,
`string_builder`, `net.socket`, `net.tls`, `net.http`, `net.ws`,
`data.json`, `data.regex`, `fs`, `proc`.

See [`docs/stdlib/`](stdlib/) for per-module API references.

---

## 11. Built-in Functions

| Function      | Description                              |
|---------------|------------------------------------------|
| `println(v)`  | Print a value followed by a newline      |
| `print(v)`    | Print a value without a newline          |
| `len(x)`      | Length: number of elements in a `list`, number of entries in a `dict`, or number of bytes in a `str` |
| `range(n)`    | Produce integer sequence `[0, n)`        |
| `assert(e)`   | Abort if expression `e` is false         |

`len` dispatches to the correct runtime function depending on the type of its
argument: `duxrt_list_len` for `list`, `duxrt_dict_len` for `dict`, and
`duxrt_str_length` for `str`.

---

## 12. Program Entry Point

The entry point is a function named `main` with return type `void` and no
parameters. It may be at the top level or inside a namespace:

```
void main() {
    println("Hello, world!")
}
```

---

## 13. Grammar Summary (EBNF-style)

```
program     = { top_decl | var_decl_stmt | ";" }
top_decl    = import_decl | func_decl | class_decl | namespace_decl
            | interface_decl | enum_decl | extern_decl

func_decl   = [ "async" ] type IDENT [ "<" type_params ">" ]
              "(" param_list? ")" [ "->" ( "get" | "set" ) ] block
class_decl  = [ { decorator } ] "class" IDENT [ "<" type_params ">" ]
              [ "(" base_list ")" ] ( ";" | "{" class_body "}" )
interface_decl = "interface" IDENT "{" method_sig* "}"
enum_decl   = "enum" IDENT "{" enum_variants "}"
namespace_decl = "namespace" dotted_name ( "{" namespace_body "}" | ";" )
import_decl = "import" dotted_name [ "as" IDENT ]
extern_decl = "extern" STRING type IDENT "(" param_list ")" ";"

stmt        = var_decl_stmt | assign_stmt | if_stmt | while_stmt
            | do_while_stmt | for_in_stmt | for_c_stmt | switch_stmt
            | match_stmt | try_catch_stmt | return_stmt | assert_stmt
            | delete_stmt | defer_stmt | throw_stmt | unsafe_stmt
            | break_stmt | continue_stmt | expr_stmt | block_stmt

type        = "int" | "long" | "real" | "double" | "bool" | "str"
            | "void" | "list" | "dict" | "tuple" | "object" | "ptr"
            | "auto" | IDENT | IDENT "<" type_args ">"
            | "fn" "(" type_list ")" "->" type
            | "const" type

string_lit  = '"' { char } '"'                  # plain string
f_string    = 'f"' { char | "{" expr "}" } '"'  # interpolated string

range_expr  = expr ".." expr
            | expr "..<" expr
            | expr "..=" expr
```

---

## 14. Memory Model

### Primitives

All primitive values (`int`, `long`, `double`, `real`, `bool`) are stack-allocated
and require no memory management.

### Strings

`str` values are heap-allocated and reference-counted.  The compiler inserts
`retain` / `release` calls automatically; the programmer does not need to free
strings explicitly.

### Lists and Dicts

`list` and `dict` values are heap-allocated and reference-counted with atomic
counters.  The compiler registers every `list`/`dict` variable in an RAII cleanup
scope; the reference count is decremented automatically at scope exit, and the
structure is freed when the count reaches zero.

Assignment (`list b = a`) shares the reference (refcount incremented).  Calling
`delete` on a `list` or `dict` releases the reference immediately and nulls the
slot; subsequent RAII cleanup at scope exit is a safe no-op.

### Class Instances

Class instances are heap-allocated via `new`.  The compiler inserts destructor
calls automatically at scope exit (RAII).  Explicit `delete` calls the destructor
and frees the memory; after `delete` the pointer is `null`.

### No Garbage Collector

Dux has no tracing GC.  Cyclic references between class instances cannot be
broken automatically; use explicit `delete` or design ownership to avoid cycles.

See [`docs/memory_model.md`](memory_model.md) for the full specification.

---

## 15. Undefined Behavior

The following constructs produce undefined behavior:

- Reading from an uninitialized variable
- Dereferencing a null pointer
- Integer overflow (wraps on two's complement hardware)
- Out-of-bounds list access (throws `IndexError` at runtime)
- Accessing a variable after `delete`

---

*End of specification.*
