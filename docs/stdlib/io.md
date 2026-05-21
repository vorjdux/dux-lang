# stdlib: io

I/O operations are built into Dux. No import is required.

---

## Functions

| Function       | Signature                                 | Description                                      |
|----------------|-------------------------------------------|--------------------------------------------------|
| `println(v)`   | `void duxrt_println_int(int64_t v)`       | Print an `int` followed by a newline             |
| `println(v)`   | `void duxrt_println_double(double v)`     | Print a `double` followed by a newline           |
| `println(v)`   | `void duxrt_println_str(const char* s)`   | Print a `str` followed by a newline              |
| `print(v)`     | `void duxrt_print_int(int64_t v)`         | Print an `int` with no trailing newline          |
| `print(v)`     | `void duxrt_print_double(double v)`       | Print a `double` with no trailing newline        |
| `print(v)`     | `void duxrt_print_str(const char* s)`     | Print a `str` with no trailing newline           |
| `readline()`   | `char* duxrt_readline(void)`              | Read one line from stdin; returns `str`          |

---

## Details

### `println(v)`

Writes the string representation of `v` to stdout followed by a newline
(`\n`). The compiler selects the correct runtime variant based on the static
type of `v`.

- `int` values are formatted as signed decimal (`%ld`).
- `double` values are formatted with `%g` (shortest representation).
- `str` values are written as-is using `puts`.

### `print(v)`

Same as `println(v)` but does not append a newline. Useful for building output
incrementally across multiple calls.

### `readline()`

Reads characters from stdin up to and including the next newline, then strips
the trailing newline before returning. The return type is `str`.

Returns an empty string if EOF is reached before any characters are read.
Input is limited to 4095 characters per call; longer lines are truncated at
that boundary.

```dux
str line = readline()
```

---

## Example

```dux
void main() {
    println("What is your name?")
    str name = readline()
    print("Hello, ")
    println(name)

    int x = 7
    print("x = ")
    println(x)          # x = 7

    double pi = 3.14159
    println(pi)         # 3.14159
}
```

---

## Implementation Notes

`println` and `print` dispatch to type-specific C functions in
`src/runtime/io.c` (`duxrt_println_int`, `duxrt_println_double`,
`duxrt_println_str`, and their `print` counterparts). The compiler resolves the
dispatch at compile time based on the expression's inferred type.

`readline` reads into a fixed 4096-byte stack buffer and copies the result to
a heap string. The caller is responsible for the string's lifetime under the
runtime memory model.
