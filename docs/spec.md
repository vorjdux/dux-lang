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
and       as        assert    bool      break     case
catch     class     const     continue  default   delete
dict      do        double    else      false     for
if        import    in        int       interface list
long      namespace new       null      object    or
private   protected public    real      return    str
switch    this      true      try       tuple     void
while
```

### 2.4 Literals

| Kind      | Example              |
|-----------|----------------------|
| Integer   | `42`, `-7`           |
| Float     | `3.14`, `-0.5`       |
| String    | `"hello"`, `"world"` |
| F-string  | `f"Hello, {name}!"`  |
| Boolean   | `true`, `false`      |
| Null      | `null`               |

String literals support standard C escape sequences (`\n`, `\t`, `\\`, `\"`, etc.).

**F-strings** (interpolated string literals) are prefixed with `f` and allow
arbitrary expressions inside `{ }` braces:

```
f"Hello, {name}!"
f"Result: {x + y}"
f"Year {year}, next {year + 1}"
```

The expression inside `{ }` must produce a value that is convertible to `str`.
F-strings are desugared at compile time into a sequence of `str` concatenations.

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
| `str`    | UTF-8 string (heap-allocated)      | `ptr`     |
| `void`   | No value (function return only)    | `void`    |

### 3.2 Composite Types

| Type     | Description                        |
|----------|------------------------------------|
| `list`   | Dynamic array (reference-counted, RAII-managed) |
| `dict`   | Hash map (string keys, reference-counted, RAII-managed) |
| `tuple`  | Fixed-size heterogeneous sequence  |
| `object` | Base type of all classes           |

**Box / unbox.** Primitive values (`int`, `double`, etc.) are stack-allocated.
When a primitive must be stored in a generic container or passed as an `object`
reference, the compiler automatically *boxes* the value — wrapping it in a
heap-allocated `object` — and *unboxes* it (unwraps and copies back to the stack)
at the use site. Boxing is implicit and transparent to the programmer.

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
```

`const` declares an immutable binding:

```
const int MAX = 100
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

### 5.9 Index Access

`list[index]`, `dict[key]`

### 5.10 Allocation / Deallocation

```
Person p = new Person("Alice", 30)
delete p
```

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
```
for int i in 0..<10 {
    ...
}
```

Iterating with `range()`:
```
for int i in range(10) {
    ...
}
```

C-style for loop:
```
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
        break &outer   // breaks the outer loop
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
return          // in void functions
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

---

## 8. Classes

```
class Point() {
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
class Resource() {
    Resource() { ... }
    ~Resource() { ... }
}
```

### 8.3 Inheritance

```
class Animal(public object) {
    ...
}

class Dog(public Animal) {
    ...
}
```

Multiple base classes are comma-separated. `object` is the implicit root class.

### 8.4 Interfaces

```
interface IShape() {
    public:
    double area()
    double perimeter()
}

class Circle(public object, private IShape) {
    ...
}
```

### 8.5 Field Default Values

Fields may have default values applied before any constructor runs:

```
class Counter() {
    int value = 0   // initialized to 0 on each new Counter()
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

Imports a standard library module. The following stdlib modules are available:
`math`. See `docs/stdlib/math.md` for the API.

---

## 11. Built-in Functions

| Function      | Description                              |
|---------------|------------------------------------------|
| `println(v)`  | Print a value followed by a newline      |
| `range(n)`    | Produce integer sequence `[0, n)`        |
| `assert(e)`   | Abort if expression `e` is false         |

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
program     = decl* stmt*
decl        = import_decl | func_decl | class_decl | namespace_decl
            | interface_decl | var_decl_stmt | const_decl

func_decl   = type IDENT '(' param_list? ')' block
class_decl  = 'class' IDENT '(' base_list? ')' ('{' member* '}' | ε)
interface_decl = 'interface' IDENT '(' ')' '{' method_sig* '}'
namespace_decl = 'namespace' dotted_name '{' decl* stmt* '}'
import_decl = 'import' dotted_name

stmt        = var_decl_stmt | assign_stmt | if_stmt | while_stmt
            | do_while_stmt | for_in_stmt | for_c_stmt | switch_stmt
            | try_catch_stmt | return_stmt | assert_stmt | delete_stmt
            | break_stmt | continue_stmt | expr_stmt | block_stmt
            | labeled_stmt

type        = 'int' | 'long' | 'real' | 'double' | 'bool' | 'str'
            | 'void' | 'list' | 'dict' | 'tuple' | 'object' | IDENT

range_expr  = expr '..' expr | expr '..<' expr | expr '..=' expr
```

---

## 14. Memory Model

All primitive values are stack-allocated. Class instances are heap-allocated
via the built-in `new` operator, and freed via `delete`. The current version
does not include a garbage collector; ownership is manual.

---

## 15. Undefined Behavior

The following constructs produce undefined behavior:

- Reading from an uninitialized variable
- Dereferencing a null pointer
- Integer overflow (wraps on two's complement hardware)
- Out-of-bounds list or array access

---

*End of specification.*
