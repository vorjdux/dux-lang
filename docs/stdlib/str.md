# stdlib: str

String operations are built into Dux.  No import is required for the `str` type
or the `f"..."` interpolation syntax.  The `str` module adds extended methods;
import it with `import str`.

---

## The `str` Type

String literals are delimited by double quotes.

```dux
str s = "hello"
str t = 'world'   # single-quote form is also valid
```

Strings are reference-counted heap values (see [`docs/memory_model.md`](../memory_model.md)).
The compiler inserts retain/release calls automatically; you never call `free` on a
string manually.

Short strings (≤ 63 bytes) are stored inline in the `DuxStr` header;
longer strings use a separately heap-allocated buffer.  In both cases the
programmer-visible semantics are the same.

---

## String Interpolation (f-strings)

The `f"..."` syntax embeds arbitrary expressions directly in a string literal.
Expressions are enclosed in `{` `}`:

```dux
str name = "World"
int x = 42
println(f"Hello {name}! x={x}")        # Hello World! x=42
println(f"{x} squared = {x * x}")      # 42 squared = 1764
println(f"positive = {x > 0}")         # positive = true
```

**Conversion rules:**

| Expression type | Result in the string |
|---|---|
| `str` | the string itself |
| `int` / `long` | decimal integer (`42` → `"42"`) |
| `double` / `real` | decimal float (`3.14` → `"3.14"`) |
| `bool` | `"true"` or `"false"` |

Escape a literal brace with `\{`:

```dux
println(f"set = \{1, 2, 3\}")   # set = {1, 2, 3}
```

F-strings can be nested in larger string expressions via `+`:

```dux
str prefix = "Result"
str msg = prefix + ": " + f"{x * x}"
```

---

## Operators

### Concatenation

The `+` operator concatenates two strings and returns a new `str`.

```dux
str greeting = "hello" + ", world"   # "hello, world"
```

### Equality

The `==` operator compares two strings by value (not by pointer).

```dux
"foo" == "foo"   # true
"foo" == "bar"   # false
```

---

## Indexing and Slicing

### Index: `s[i]`

Returns a single-character `str` at position `i`. Negative indices count from
the end of the string. Returns an empty string if `i` is out of range.

```dux
str s = "hello"
str c = s[1]     # "e"
str last = s[-1] # "o"
```

Backed by `duxrt_str_index(const char* s, int64_t i)`.

### Slice: `s[start..end]`

Returns a new `str` containing the characters from index `start` up to (but
not including) index `end`. If `start >= end` or the range is entirely
out of bounds, an empty string is returned. `start` is clamped to `0`; `end`
is clamped to `len(s)`.

```dux
str s = "hello"
str sub = s[1..4]   # "ell"
str all = s[0..5]   # "hello"
```

Backed by `duxrt_str_slice(const char* s, int64_t start, int64_t end)`.

---

## Built-in Functions

| Function    | Signature                            | Description                                    |
|-------------|--------------------------------------|------------------------------------------------|
| `len(s)`    | `int64_t duxrt_str_length(const char* s)` | Number of bytes in the string           |
| `str(x)`    | `char* duxrt_str_from_int(int64_t v)` | Convert an `int` to its decimal string form   |
| `str(x)`    | `char* duxrt_str_from_double(double v)` | Convert a `double` to its string form       |

`len(s)` returns `0` for a null/empty string.

`str(x)` dispatches at compile time based on the type of `x`:
- `int` values are formatted as a signed decimal integer (`%ld`).
- `double` values are formatted using `%g` (shortest representation).

---

## Example

```dux
void main() {
    str name = "Dux"
    str msg  = "Hello, " + name + "!"
    println(msg)               # Hello, Dux!

    println(len(msg))          # 11

    str first = msg[0]
    println(first)             # H

    str sub = msg[7..10]
    println(sub)               # Dux

    int n = 42
    str ns = str(n)
    println(ns)                # 42

    double pi = 3.14159
    println(str(pi))           # 3.14159
}
```

---

## Implementation Notes

All string values in Dux are heap-allocated, null-terminated C strings. The
runtime functions in `src/runtime/str.c` handle allocation; memory is managed
by the runtime GC layer (`duxrt_alloc` / `duxrt_free`).
