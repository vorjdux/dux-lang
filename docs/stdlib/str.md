# stdlib: str

String operations are built into Dux. No import is required.

---

## The `str` Type

String literals are delimited by double quotes. The type name is `str`.

```dux
str s = "hello"
```

Strings are null-terminated byte arrays managed by the runtime. All string
operations that produce a new string allocate fresh memory via `duxrt_alloc`.

---

## Operators

### Concatenation

The `+` operator concatenates two strings and returns a new `str`.

```dux
str greeting = "hello" + ", world"   # "hello, world"
```

Backed by `duxrt_str_concat(const char* a, const char* b)`.

### Equality

The `==` operator compares two strings by value (not by pointer).

```dux
"foo" == "foo"   # true
"foo" == "bar"   # false
```

Backed by `duxrt_str_eq(const char* a, const char* b)`.

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
