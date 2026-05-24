# Dux Language Grammar Reference

This document is the formal grammar reference for the Dux programming language.
Rules are written in EBNF notation: `rule ::= production | ...`
Optional elements are written as `[ ... ]`, zero-or-more as `{ ... }`, and
one-or-more as `( ... )+`.

---

## 1. Lexical Structure

### 1.1 Character set

Source files are UTF-8 encoded text.  Line endings may be `LF` or `CRLF`;
the lexer normalises them to `LF` internally.

### 1.2 Whitespace and comments

Whitespace (spaces, tabs, carriage returns) is ignored except as a token
separator and as the trigger for automatic semicolon insertion (see §1.6).

```ebnf
line_comment  ::= "#" { any_char_except_newline }
block_comment ::= '"""' { any_char } '"""'
```

Single-line comments start with `#` and run to end of line.
Triple-quoted `"""..."""` spans are treated as block comments (doc-strings).

### 1.3 Identifiers

```ebnf
ident   ::= alpha { alnum }
alpha   ::= [a-zA-Z_]
alnum   ::= [a-zA-Z0-9_]
```

### 1.4 Keywords

The following identifiers are reserved and cannot be used as user-defined names:

```
and        as         assert     async      auto       await
bool       break      case       catch      class      const
continue   default    defer      delete     dict       do
double     else       enum       extern     false      fn
for        from       if         import     in         interface
list       long       match      namespace  new        not
null       object     or         private    protected  public
ptr        real       return     static     str        super
switch     this       thread_local throw     true       try
tuple      unsafe     void       while
```

The tokens `__get__` and `__set__` are also reserved (property accessor markers).

### 1.5 Literals

```ebnf
integer_lit ::= digit+
              | digit+ "i"          (* explicit int suffix *)
              | digit+ "l"          (* long suffix *)
              | digit+ "d"          (* double from integer, e.g. 42d *)
              | digit+ "f"          (* real (32-bit) from integer, e.g. 42f *)

float_lit   ::= digit+ "." digit*
              | "." digit+
              | digit+ "." digit* "d"   (* explicit double suffix *)
              | "." digit+ "d"

real_lit    ::= digit+ "." digit* "f"   (* 32-bit float suffix *)
              | "." digit+ "f"

long_lit    ::= digit+ "l"

bool_lit    ::= "true" | "false"

string_lit  ::= '"' { str_char_dq } '"'
              | "'" { str_char_sq } "'"

str_char_dq ::= any_char_except_dquote_and_newline
              | "\n" | "\t" | "\r" | "\\" | '\"' | "\'" | "\{"

str_char_sq ::= any_char_except_squote_and_newline
              | "\n" | "\t" | "\r" | "\\" | '\"' | "\'" | "\{"

null_lit    ::= "null"
```

**Literal type summary:**

| Literal form | Inferred type |
|---|---|
| `42` | `int` |
| `42i` | `int` |
| `42l` | `long` |
| `42d` | `double` |
| `42f` | `real` |
| `3.14` | `double` |
| `3.14d` | `double` |
| `3.14f` | `real` |
| `"hello"` | `str` |
| `true` / `false` | `bool` |
| `null` | (null pointer) |

### 1.6 Automatic semicolon insertion

Dux uses a Go-style automatic semicolon rule.  The lexer sets an internal
`need_semi` flag whenever it emits a token that can end a statement.
When the *next* newline is scanned and the flag is set (and brace/bracket
nesting depth is zero), the lexer synthesises and emits a `SEMI` token
before consuming the newline.

Tokens that set `need_semi = true`:

- Identifiers and all literal tokens (integer, float, real, long, string, bool)
- `this`, `super`, `null`, `true`, `false`
- `return`, `break`, `continue`, `await`
- `)`, `]`, `}`
- `++`, `--`
- The keyword `str` (when used as an expression)

Tokens that clear `need_semi = false` (all others, including keywords like
`if`, `while`, `for`, `class`, operators, `(`, `[`, `{`, `->`).

**Practical rules:**

- Always place the closing `}` of a block on its own line.
- When writing a one-liner block body, insert an explicit `;` before `}`.
- Opening parentheses `(` and brackets `[` suppress semicolon insertion for
  their entire contents (nesting counter tracks depth).

---

## 2. Types

### 2.1 Primitive types

```ebnf
primitive_type ::= "void"
                 | "int"
                 | "long"
                 | "real"
                 | "double"
                 | "bool"
                 | "str"
                 | "list"
                 | "dict"
                 | "tuple"
                 | "object"
                 | "ptr"
```

| Type | Size | Description |
|---|---|---|
| `void` | — | No value; only valid as a function return type |
| `int` | 32-bit | Signed integer |
| `long` | 64-bit | Signed integer |
| `double` | 64-bit | IEEE 754 floating-point |
| `real` | 32-bit | IEEE 754 floating-point |
| `bool` | 1-bit | Boolean (`true` / `false`) |
| `str` | — | Reference-counted string (hybrid inline/heap) |
| `list` | — | Dynamic array |
| `dict` | — | Hash map |
| `tuple` | — | Fixed-length heterogeneous sequence |
| `object` | — | Base type for heap-allocated class instances |
| `ptr` | word | Raw pointer (C FFI / unsafe code) |

### 2.2 Class types

A user-defined class name is used directly as a type:

```ebnf
class_type ::= ident
```

### 2.3 Generic (parameterised) types

```ebnf
generic_type ::= ident "<" type_arg_list_ne ">"
type_arg_list_ne ::= type_expr { "," type_expr }
```

Example: `Box<int>`, `Pair<str, int>`.

### 2.4 Function types

```ebnf
fn_type ::= "fn" "(" fn_type_params ")" "->" type_expr
fn_type_params ::= [ type_expr { "," type_expr } ]
```

Example: `fn(int) -> int`, `fn(str, int) -> bool`.

### 2.5 Qualified type expressions

```ebnf
type_expr ::= primitive_type
            | ident
            | ident "<" type_arg_list_ne ">"
            | fn_type
            | "const" type_expr
```

The `const` qualifier may prefix any type expression to mark the variable
as immutable.

---

## 3. Declarations

### 3.1 Program structure

```ebnf
program       ::= { top_decl | ";" }
top_decl      ::= namespace_decl
                | import_decl
                | class_decl
                | interface_decl
                | enum_decl
                | func_decl
                | extern_decl
```

### 3.2 Namespace

```ebnf
namespace_decl ::= "namespace" dotted_name "{" namespace_body "}"
                 | "namespace" dotted_name ";"

namespace_body ::= { ";" | decl | var_decl_stmt | simple_stmt ";" }

dotted_name    ::= ident { "." ident }
```

A `namespace foo;` (with semicolon, no body) is a package declaration that
assigns the current file to a namespace without opening a body block.

### 3.3 Import

```ebnf
import_decl ::= "import" dotted_name ";"
              | "import" dotted_name "as" ident ";"
              | "import" dotted_name "::" "{" ident_list "}" ";"
              | "import" dotted_name "::" "{" ident_list "}" "as" ident ";"
              | "import" dotted_name "::" "*" ";"
              | "import" "{" ident_list "}" "from" dotted_name ";"

ident_list  ::= ident { "," ident }
```

Examples:
```dux
import math
import "./utils" as utils
import std::{ println, eprintln }
import { factorial } from "./algorithms"
import io::*
```

### 3.4 Extern declaration (C FFI)

```ebnf
extern_decl ::= "extern" string_lit type_expr ident "(" param_list ")" ";"
```

Example:
```dux
extern "C" ptr malloc(long size)
extern "C" void free(ptr p)
```

### 3.5 Function declaration

```ebnf
func_decl ::= type_expr ident opt_type_params "(" param_list ")" [ opt_func_modifier ] block
            | "async" type_expr ident opt_type_params "(" param_list ")" block
            | "static" type_expr ident "(" param_list ")" [ opt_func_modifier ] block

opt_func_modifier ::= "->" "get"
                    | "->" "set"
                    | "__get__"
                    | "__set__"

opt_type_params   ::= [ "<" type_params_ne ">" ]
type_params_ne    ::= type_param { "," type_param }
type_param        ::= ident [ ":" ident ]      (* name [ : bound ] *)

param_list        ::= [ param { "," param } ]
param             ::= type_expr ident
```

Examples:
```dux
int add(int a, int b) { return a + b }

void greet(str name) { println("Hello, " + name) }

# Generic function
int identity<T>(T x) { return x }

# Async function
async void fetch(str url) { ... }

# Property getter
double radius() -> get { return this._radius }
```

### 3.6 Class declaration

```ebnf
class_decl ::= { decorator } "class" ident [ opt_type_params ] [ "(" base_list ")" ]
               ( ";" | "{" class_body "}" )

base_list  ::= base_entry { "," base_entry }
base_entry ::= [ access_mod ] class_ref
class_ref  ::= ident | "object"

class_body    ::= { class_member | ";" }
class_member  ::= access_mod ":"
                | { decorator } func_decl
                | { decorator } field_decl
                | { decorator } ctor_decl
                | { decorator } dtor_decl

access_mod    ::= "public" | "private" | "protected"
```

#### 3.6.1 Field declaration

```ebnf
field_decl ::= type_expr ident ";"
             | type_expr ident "=" expr ";"
             | "static" type_expr ident ";"
             | "static" type_expr ident "=" expr ";"
```

#### 3.6.2 Constructor

```ebnf
ctor_decl ::= ident "(" param_list ")" [ ":" init_list ] block

init_list  ::= init_entry { "," init_entry }
init_entry ::= ident "(" arg_list ")"
```

The `ident` must match the class name.  The optional `: init_list` is an
initialiser list that calls base-class constructors:

```dux
class Dog(Animal) {
    Dog(str n) : Animal(n) { }
}
```

#### 3.6.3 Destructor

```ebnf
dtor_decl ::= "~" ident "(" ")" block
```

Destructors are called automatically at scope exit (RAII).

#### 3.6.4 Access modifiers

Access labels apply to all members declared after them (until the next label):

```dux
class Point {
    public:
    int x
    int y

    private:
    int _cache
}
```

### 3.7 Interface declaration

```ebnf
interface_decl ::= "interface" ident "(" ")" "{" interface_members "}"
interface_members ::= { access_mod ":" | interface_method | ";" }
interface_method  ::= type_expr ident "(" param_list ")" ";"
```

### 3.8 Enum declaration

```ebnf
enum_decl    ::= "enum" ident "{" enum_variants "}" [ ";" ]
enum_variants ::= [ enum_variant { ( "," | ";" ) enum_variant } [ "," ] ]
enum_variant  ::= ident [ "(" enum_payload ")" ]
enum_payload  ::= type_expr { "," type_expr }
```

Examples:
```dux
enum Color { Red, Green, Blue }

enum Shape {
    Circle(double),
    Rect(double, double),
}
```

### 3.9 Decorators

```ebnf
decorator      ::= "@" decorator_name "(" arg_list ")" ";"
                 | "@" decorator_name ";"
decorator_name ::= ident
                 | ident "::" ident
```

Decorators appear before class or function declarations:
```dux
@deprecated;
@module::annotation(42);
void old_fn() { ... }
```

---

## 4. Statements

```ebnf
stmt ::= if_stmt
       | while_stmt
       | do_while_stmt
       | for_stmt
       | switch_stmt
       | match_stmt
       | try_stmt
       | return_stmt
       | break_stmt
       | continue_stmt
       | assert_stmt
       | delete_stmt
       | defer_stmt
       | throw_stmt
       | unsafe_stmt
       | var_decl_stmt
       | simple_stmt ";"

simple_stmt ::= expr
```

### 4.1 Block

```ebnf
block      ::= "{" stmt_list "}"
stmt_list  ::= { ";" | stmt }
```

### 4.2 If / else

```ebnf
if_stmt ::= "if" expr block
          | "if" expr block "else" block
          | "if" expr block "else" if_stmt
```

The dangling-else is resolved by binding `else` to the innermost `if`.

```dux
if x > 10 {
    println("big")
} else if x > 3 {
    println("medium")
} else {
    println("small")
}
```

### 4.3 While

```ebnf
while_stmt ::= [ opt_label ] "while" expr block
opt_label  ::= "&" ident
```

```dux
while i < 5 { i += 1 }

&outer while true {
    while k < 5 { if k == 2 { break &outer } ; k++ }
}
```

### 4.4 Do-while

```ebnf
do_while_stmt ::= "do" block "while" expr ";"
```

```dux
do { x++ } while x < 3
```

### 4.5 For

```ebnf
for_stmt ::= "for" type_expr ident "in" expr block
           | "for" type_expr ident "=" expr "," expr "," expr block
```

The first form is a range-based for-in loop.  The second is a C-style loop
with `init, cond, step` comma-separated (no parentheses):

```dux
for int i in 0..<10 { println(i) }
for int i in 1..=5  { println(i) }
for int i = 0, i < 5, i++ { println(i) }
```

### 4.6 Switch

```ebnf
switch_stmt  ::= "switch" expr "{" switch_cases "}"
switch_cases ::= { switch_case }
switch_case  ::= "case" expr ":" stmt_list
               | "default" ":" stmt_list
```

Cases fall through unless terminated with `break`:

```dux
switch x {
    case 1: println("one"); break
    case 2: println("two"); break
    default: println("other")
}
```

### 4.7 Match

```ebnf
match_stmt   ::= "match" expr "{" match_arms "}"
match_arms   ::= { match_arm | ";" }
match_arm    ::= match_pattern "=>" block [ ";" ]

match_pattern ::= ident "." ident                      (* EnumName.Variant *)
                | ident "." ident "(" ident_list ")"   (* payload destructure *)
                | integer_lit
                | bool_lit
                | string_lit
                | "null"
                | ident                                 (* wildcard *)
```

The wildcard pattern is a bare identifier (conventionally `_`).
Match is exhaustive — all possible values must be covered:

```dux
match n {
    1 => { println("one") }
    2 => { println("two") }
    _ => { println("other") }
}

match shape {
    Shape.Circle(r)     => { println("circle") }
    Shape.Rect(w, h)    => { println("rect") }
}
```

### 4.8 Try / catch

```ebnf
try_stmt ::= "try" block "catch" "..." block
           | "try" block "catch" "(" type_expr ident ")" block
```

`catch ...` catches any thrown value.  `catch (Type var)` binds the
caught object to `var` for typed handling:

```dux
try { risky() } catch ... { println("error") }
try { throw new AppError("oops") } catch (AppError e) { println(e.message) }
```

### 4.9 Return

```ebnf
return_stmt ::= "return" ";"
              | "return" expr ";"
```

### 4.10 Break and continue

```ebnf
break_stmt    ::= "break" ";"
                | "break" "&" ident ";"

continue_stmt ::= "continue" ";"
                | "continue" "&" ident ";"
```

Labeled break/continue use the `&label` syntax to target an outer loop:

```dux
break &outer
continue &loop
```

### 4.11 Assert

```ebnf
assert_stmt ::= "assert" "(" expr ")" ";"
```

Aborts with a runtime error if the expression evaluates to `false`.

### 4.12 Delete

```ebnf
delete_stmt ::= "delete" expr ";"
```

Explicitly frees a heap-allocated object.

### 4.13 Defer

```ebnf
defer_stmt ::= "defer" "{" stmt_list "}"
             | "defer" defer_expr ";"

defer_expr   ::= defer_primary
               | defer_expr "." ident
               | defer_expr "(" arg_list ")"
               | defer_expr "[" expr "]"

defer_primary ::= ident | "this" | "super" | "(" expr ")"
```

Deferred statements run at end of scope in LIFO order.  The expression
form does not accept a bare `{` to avoid ambiguity with the block form:

```dux
defer { println("cleanup") }
defer handle.close()
```

### 4.14 Throw

```ebnf
throw_stmt ::= "throw" expr ";"
```

Any value may be thrown (typically a class instance).

### 4.15 Unsafe block

```ebnf
unsafe_stmt ::= "unsafe" block
```

Permits raw-pointer operations and C FFI calls within the block.

### 4.16 Variable declaration

```ebnf
var_decl_stmt ::= type_expr var_decl_items ";"
                | "static" type_expr var_decl_items ";"
                | "static" "const" type_expr var_decl_items ";"
                | "const" "static" type_expr var_decl_items ";"
                | "thread_local" type_expr var_decl_items ";"
                | "thread_local" "const" type_expr var_decl_items ";"
                | "const" "thread_local" type_expr var_decl_items ";"
                | "auto" ident "=" expr ";"

var_decl_items ::= var_decl_item { "," var_decl_item }
var_decl_item  ::= ident [ "=" expr ]
```

Multiple variables of the same type may be declared in one statement:

```dux
int x, y = 0, z
auto name = "Dux"
static int counter = 0
const int MAX = 100
thread_local int tid = 0
```

---

## 5. Expressions

Operator precedence (lowest to highest):

| Precedence | Operators | Associativity |
|---|---|---|
| 1 (lowest) | `=` `+=` `-=` `*=` `/=` `%=` | Right |
| 2 | `\|\|` `or` | Left |
| 3 | `&&` `and` | Left |
| 4 | `==` `!=` | Left |
| 5 | `<` `>` `<=` `>=` `..<` `..=` | Left |
| 6 | `+` `-` | Left |
| 7 | `*` `/` `%` | Left |
| 8 | `!` `~` unary `-` unary `+` `++` `--` (prefix) | Right |
| 9 (highest) | `++` `--` (postfix) `.` `[` `(` | Left |

```ebnf
expr        ::= assign_expr

assign_expr ::= postfix_expr "="  assign_expr
              | postfix_expr "+=" assign_expr
              | postfix_expr "-=" assign_expr
              | postfix_expr "*=" assign_expr
              | postfix_expr "/=" assign_expr
              | postfix_expr "%=" assign_expr
              | or_expr

or_expr     ::= or_expr "||" and_expr
              | or_expr "or" and_expr
              | and_expr

and_expr    ::= and_expr "&&" eq_expr
              | and_expr "and" eq_expr
              | eq_expr

eq_expr     ::= eq_expr "==" rel_expr
              | eq_expr "!=" rel_expr
              | rel_expr

rel_expr    ::= rel_expr "<"   add_expr
              | rel_expr ">"   add_expr
              | rel_expr "<="  add_expr
              | rel_expr ">="  add_expr
              | rel_expr "..<" add_expr
              | rel_expr "..=" add_expr
              | add_expr

add_expr    ::= add_expr "+" mul_expr
              | add_expr "-" mul_expr
              | mul_expr

mul_expr    ::= mul_expr "*" unary_expr
              | mul_expr "/" unary_expr
              | mul_expr "%" unary_expr
              | unary_expr

unary_expr  ::= "!"  unary_expr
              | "~"  unary_expr
              | "-"  unary_expr
              | "+"  unary_expr
              | "++" unary_expr
              | "--" unary_expr
              | "not" unary_expr
              | "await" unary_expr
              | postfix_expr

postfix_expr ::= postfix_expr "." ident
               | postfix_expr "[" expr "]"
               | postfix_expr "(" arg_list ")"
               | postfix_expr "++"
               | postfix_expr "--"
               | primary_expr

primary_expr ::= integer_lit
               | float_lit
               | real_lit
               | long_lit
               | string_lit
               | bool_lit
               | "null"
               | ident
               | "str"
               | "this"
               | "super"
               | "(" expr ")"
               | "new" type_expr "(" arg_list ")"
               | list_lit
               | dict_lit
               | lambda_expr

arg_list     ::= [ expr { "," expr } ]
```

### 5.1 Arithmetic

```dux
int a = 10 + 3   # 13
int b = 10 - 3   # 7
int c = 10 * 3   # 30
int d = 10 / 3   # 3  (integer division when both operands are int)
int e = 10 % 3   # 1
```

### 5.2 Comparison

```dux
a < b   a > b   a <= b   a >= b   a == b   a != b
```

Range operators produce a range value for use in `for...in`:
```dux
0..<10   # exclusive: 0 to 9
1..=5    # inclusive: 1 to 5
```

### 5.3 Logical

```dux
true && false   # and (symbol form)
true || false   # or  (symbol form)
!true           # not (symbol form)

true and false  # and (keyword form)
true or  false  # or  (keyword form)
not true        # not (keyword form)
```

### 5.4 String concatenation

```dux
str s = "hello" + ", " + "world"
```

The `+` operator is overloaded for `str` operands.

### 5.5 Member access

```dux
obj.field
obj.method(args)
```

### 5.6 Indexing

```dux
list[0]
dict["key"]
obj[expr]     # calls operator__index if defined
```

### 5.7 New expression

```ebnf
new_expr ::= "new" type_expr "(" arg_list ")"
```

Allocates a class instance on the heap and calls its constructor:

```dux
Dog d = new Dog("Rex")
Box<int> b = new Box<int>(42)
```

### 5.8 Lambda expressions

```ebnf
lambda_expr ::= "fn" "(" param_list ")" "=>" expr
              | "fn" "(" param_list ")" block
              | "fn" "(" param_list ")" "->" type_expr block
              | "fn" "(" param_list ")" "->" type_expr "=>" expr
```

Closures capture variables from the enclosing scope by value:

```dux
auto sq      = fn(int n) -> int => n * n
auto greet   = fn(str name) -> void { println("hello, " + name) }
auto addBase = fn(int x) -> int => x + base   # captures 'base'
```

---

## 6. Property Accessors

A function declared with `-> get` or `-> set` after its parameter list
becomes a property accessor.  The modifiers are written as:

```ebnf
opt_func_modifier ::= "->" "get"
                    | "->" "set"
                    | "__get__"
                    | "__set__"
```

Getter — called as `obj.property` (no parentheses):

```dux
double radius() -> get {
    return this._radius
}
```

Setter — called as `obj.property = value`:

```dux
void radius(double value) -> set {
    this._radius = value
}
```

Usage:

```dux
Circle c = new Circle(5.0)
double r = c.radius       # invokes getter
c.radius = 3.0            # invokes setter
```

---

## 7. Generics

### 7.1 Generic classes

```ebnf
class_decl ::= "class" ident "<" type_params_ne ">" ...
type_params_ne ::= type_param { "," type_param }
type_param     ::= ident [ ":" ident ]
```

The optional `: Bound` syntax constrains the type parameter to implement
a given interface:

```dux
class Box<T> {
    T value
    Box(T v) { this.value = v }
    T unwrap() { return this.value }
}

class Pair<A, B> {
    A first
    B second
}
```

Instantiation:

```dux
Box<int>      bi = new Box<int>(42)
Pair<int,str> p  = new Pair<int,str>(7, "seven")
```

### 7.2 Generic functions

```ebnf
func_decl ::= type_expr ident "<" type_params_ne ">" "(" param_list ")" block
```

```dux
T identity<T>(T x) { return x }
void swap<T>(T a, T b) { auto tmp = a; a = b; b = tmp }
```

Each call site is monomorphised — a separate native instantiation is
generated per distinct set of type arguments.

---

## 8. Auto Type Inference

```ebnf
var_decl_stmt ::= "auto" ident "=" expr ";"
```

The compiler deduces the type from the initialiser expression.  `auto` may
only appear at the variable declaration level; it cannot be used in parameter
lists or return types:

```dux
auto name  = "Dux"         # str
auto count = 0             # int
auto ratio = 1.5           # double
auto big   = 9000000000l   # long
auto temp  = 98.6f         # real
auto sq    = fn(int n) -> int => n * n   # fn(int) -> int
```

---

## 9. Collection Literals

### 9.1 List literals

```ebnf
list_lit ::= "[" "]"
           | "[" expr_list "]"
expr_list ::= expr { "," expr }
```

```dux
auto empty = []
auto nums  = [1, 2, 3, 4, 5]
auto mixed = ["a", "b", "c"]
```

### 9.2 Dict literals

```ebnf
dict_lit  ::= "{" dict_pairs "}"
dict_pairs ::= [ dict_pair { "," dict_pair } [ "," ] ]
dict_pair  ::= expr ":" expr
```

```dux
auto empty = {}
auto ages  = {"alice": 30, "bob": 25}
auto table = {1: "one", 2: "two"}
```

---

## 10. Operator Overloading

Classes may define special methods to overload built-in operators.
The methods follow the naming convention `operator__<op>`:

| Operator | Method name |
|---|---|
| `+` | `operator__add` |
| `-` | `operator__sub` |
| `*` | `operator__mul` |
| `/` | `operator__div` |
| `%` | `operator__mod` |
| `==` | `operator__eq` |
| `!=` | `operator__ne` |
| `<` | `operator__lt` |
| `>` | `operator__gt` |
| `<=` | `operator__le` |
| `>=` | `operator__ge` |
| `[]` | `operator__index` |

```dux
class Vec2 {
    int x
    int y
    Vec2(int x, int y) { this.x = x; this.y = y }
    Vec2 operator__add(Vec2 other) {
        return new Vec2(this.x + other.x, this.y + other.y)
    }
    int operator__index(int i) {
        if i == 0 { return this.x }
        return this.y
    }
}
```

---

## 11. Concurrency

### 11.1 Async functions

```ebnf
func_decl ::= "async" type_expr ident opt_type_params "(" param_list ")" block
```

### 11.2 Await expression

```ebnf
unary_expr ::= "await" unary_expr | ...
```

```dux
async void fetch(str url) {
    str body = await http_get(url)
    println(body)
}
```

---

## 12. Complete Grammar Summary

```ebnf
(* Program *)
program         ::= { top_decl | ";" }
top_decl        ::= namespace_decl | import_decl | class_decl
                  | interface_decl | enum_decl | func_decl | extern_decl

(* Namespace *)
namespace_decl  ::= "namespace" dotted_name "{" namespace_body "}"
                  | "namespace" dotted_name ";"
namespace_body  ::= { ";" | decl | var_decl_stmt | simple_stmt ";" }
dotted_name     ::= ident { "." ident }

(* Import *)
import_decl     ::= "import" dotted_name ";"
                  | "import" dotted_name "as" ident ";"
                  | "import" dotted_name "::" "{" ident_list "}" ";"
                  | "import" dotted_name "::" "{" ident_list "}" "as" ident ";"
                  | "import" dotted_name "::" "*" ";"
                  | "import" "{" ident_list "}" "from" dotted_name ";"
ident_list      ::= ident { "," ident }

(* Extern *)
extern_decl     ::= "extern" string_lit type_expr ident "(" param_list ")" ";"

(* Class *)
class_decl      ::= { decorator } "class" ident [ "<" type_params_ne ">" ]
                    [ "(" base_list ")" ] ( ";" | "{" class_body "}" )
base_list       ::= base_entry { "," base_entry }
base_entry      ::= [ access_mod ] ( ident | "object" )
class_body      ::= { class_member | ";" }
class_member    ::= access_mod ":"
                  | { decorator } ( func_decl | field_decl | ctor_decl | dtor_decl )
field_decl      ::= [ "static" ] type_expr ident [ "=" expr ] ";"
ctor_decl       ::= ident "(" param_list ")" [ ":" init_list ] block
dtor_decl       ::= "~" ident "(" ")" block
init_list       ::= ident "(" arg_list ")" { "," ident "(" arg_list ")" }
access_mod      ::= "public" | "private" | "protected"

(* Interface *)
interface_decl  ::= "interface" ident "(" ")" "{" interface_members "}"
interface_members ::= { access_mod ":" | type_expr ident "(" param_list ")" ";" | ";" }

(* Enum *)
enum_decl       ::= "enum" ident "{" [ enum_variant { ( "," | ";" ) enum_variant } [ "," ] ] "}" [ ";" ]
enum_variant    ::= ident [ "(" type_expr { "," type_expr } ")" ]

(* Decorator *)
decorator       ::= "@" decorator_name [ "(" arg_list ")" ] ";"
decorator_name  ::= ident | ident "::" ident

(* Function *)
func_decl       ::= type_expr ident [ "<" type_params_ne ">" ] "(" param_list ")"
                    [ opt_func_modifier ] block
                  | "async" type_expr ident [ "<" type_params_ne ">" ] "(" param_list ")" block
                  | "static" type_expr ident "(" param_list ")" [ opt_func_modifier ] block
opt_func_modifier ::= ( "->" | ) ( "get" | "set" )
type_params_ne  ::= ident [ ":" ident ] { "," ident [ ":" ident ] }
param_list      ::= [ param { "," param } ]
param           ::= type_expr ident

(* Types *)
type_expr       ::= "void" | "int" | "long" | "real" | "double" | "str" | "bool"
                  | "list" | "dict" | "tuple" | "object" | "ptr"
                  | ident
                  | ident "<" type_expr { "," type_expr } ">"
                  | "fn" "(" [ type_expr { "," type_expr } ] ")" "->" type_expr
                  | "const" type_expr

(* Statements *)
stmt            ::= if_stmt | while_stmt | do_while_stmt | for_stmt
                  | switch_stmt | match_stmt | try_stmt
                  | return_stmt | break_stmt | continue_stmt
                  | assert_stmt | delete_stmt | defer_stmt
                  | throw_stmt | unsafe_stmt | var_decl_stmt
                  | simple_stmt ";"
block           ::= "{" { ";" | stmt } "}"
if_stmt         ::= "if" expr block [ "else" ( block | if_stmt ) ]
while_stmt      ::= [ "&" ident ] "while" expr block
do_while_stmt   ::= "do" block "while" expr ";"
for_stmt        ::= "for" type_expr ident "in" expr block
                  | "for" type_expr ident "=" expr "," expr "," expr block
switch_stmt     ::= "switch" expr "{" { ( "case" expr | "default" ) ":" { stmt } } "}"
match_stmt      ::= "match" expr "{" match_arm { match_arm | ";" } "}"
match_arm       ::= match_pattern "=>" block [ ";" ]
match_pattern   ::= ident "." ident [ "(" ident_list ")" ]
                  | integer_lit | bool_lit | string_lit | "null" | ident
try_stmt        ::= "try" block "catch" ( "..." | "(" type_expr ident ")" ) block
return_stmt     ::= "return" [ expr ] ";"
break_stmt      ::= "break" [ "&" ident ] ";"
continue_stmt   ::= "continue" [ "&" ident ] ";"
assert_stmt     ::= "assert" "(" expr ")" ";"
delete_stmt     ::= "delete" expr ";"
defer_stmt      ::= "defer" block | "defer" defer_expr ";"
throw_stmt      ::= "throw" expr ";"
unsafe_stmt     ::= "unsafe" block
var_decl_stmt   ::= [ "static" | "thread_local" ] [ "const" ] type_expr var_decl_items ";"
                  | "auto" ident "=" expr ";"
var_decl_items  ::= ident [ "=" expr ] { "," ident [ "=" expr ] }

(* Expressions *)
expr            ::= postfix_expr assign_op assign_expr | or_expr
assign_op       ::= "=" | "+=" | "-=" | "*=" | "/=" | "%="
or_expr         ::= or_expr ( "||" | "or" ) and_expr | and_expr
and_expr        ::= and_expr ( "&&" | "and" ) eq_expr | eq_expr
eq_expr         ::= eq_expr ( "==" | "!=" ) rel_expr | rel_expr
rel_expr        ::= rel_expr ( "<" | ">" | "<=" | ">=" | "..<" | "..=" ) add_expr | add_expr
add_expr        ::= add_expr ( "+" | "-" ) mul_expr | mul_expr
mul_expr        ::= mul_expr ( "*" | "/" | "%" ) unary_expr | unary_expr
unary_expr      ::= ( "!" | "~" | "-" | "+" | "++" | "--" | "not" | "await" ) unary_expr
                  | postfix_expr
postfix_expr    ::= postfix_expr ( "." ident | "[" expr "]" | "(" arg_list ")" | "++" | "--" )
                  | primary_expr
primary_expr    ::= integer_lit | float_lit | real_lit | long_lit | string_lit
                  | bool_lit | "null" | ident | "str" | "this" | "super"
                  | "(" expr ")"
                  | "new" type_expr "(" arg_list ")"
                  | "[" [ expr { "," expr } ] "]"
                  | "{" [ expr ":" expr { "," expr ":" expr } [ "," ] ] "}"
                  | "fn" "(" param_list ")" ( "=>" expr | block | "->" type_expr ( block | "=>" expr ) )
arg_list        ::= [ expr { "," expr } ]
```
