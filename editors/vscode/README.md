# Dux Language — VSCode / Cursor Extension

Full language support for the **Dux** programming language (`.dux` files) in Visual Studio Code and Cursor.

---

## Features

- **Syntax Highlighting** — complete tokenisation of all Dux constructs:
  - Control-flow keywords (`if`, `else`, `for`, `while`, `do`, `switch`, `match`, `case`, `default`, `break`, `continue`, `return`, `try`, `catch`, `throw`, `in`, `defer`)
  - Declaration keywords (`class`, `interface`, `enum`, `fn`, `namespace`, `import`, `from`, `as`, `extern`, `unsafe`, `async`, `await`, `new`, `delete`)
  - Type keywords (`void`, `int`, `long`, `real`, `double`, `str`, `bool`, `list`, `dict`, `tuple`, `object`, `ptr`, `auto`)
  - Modifier keywords (`public`, `private`, `protected`, `static`, `const`, `thread_local`, `override`)
  - Literal constants (`true`, `false`, `null`)
  - Word operators (`and`, `or`, `not`)
  - Built-in functions (`println`, `print`, `readline`, `len`, `range`, `assert`)
  - Special identifiers (`this`, `super`, `__get__`, `__set__`)
  - PascalCase type references highlighted as type names
  - Function and type definitions with named captures
  - All operator categories (arithmetic, comparison, assignment, bitwise, logical, arrow, range)
- **String Support**
  - Double-quoted strings `"..."`
  - Single-quoted strings `'...'`
  - Interpolated f-strings `f"...{expr}..."` with full embedded-expression highlighting
  - Escape sequences (`\n`, `\t`, `\\`, `\uXXXX`, …)
- **Number Literals** — integers, floats, hexadecimal (`0x…`), binary (`0b…`)
- **Comments** — line comments (`//`) and block comments (`/* … */`), including nested block comments
- **Bracket Matching & Auto-closing** — `{}`, `[]`, `()`, `""`, `''`
- **Auto-indentation** — indent increases after `{`, decreases before `}`
- **Code Folding** — region markers (`// #region` / `// #endregion`) and brace-based folding
- **Snippets** — common boilerplate for functions, classes, loops, and more

---

## Installation

### From the Marketplace (VS Code)

1. Open VS Code.
2. Press `Ctrl+P` (macOS: `Cmd+P`) and run:
   ```
   ext install dux-lang.dux-lang
   ```
3. Files ending in `.dux` will be automatically detected.

### From a `.vsix` File (VS Code & Cursor)

1. Download the latest `.vsix` from the [Releases page](https://github.com/vorjdux/dux-lang/releases).
2. In VS Code / Cursor: **Extensions** → `…` menu → **Install from VSIX…** → select the file.
3. Alternatively, from the terminal:
   ```bash
   code --install-extension dux-lang-<version>.vsix
   # or for Cursor:
   cursor --install-extension dux-lang-<version>.vsix
   ```

### OpenVSX (Cursor / VSCodium)

The extension is published on [open-vsx.org](https://open-vsx.org/extension/dux-lang/dux-lang).  
In Cursor, search `dux-lang` in the Extensions panel — it will be found automatically via OpenVSX.

---

## Usage

Open any `.dux` file — syntax highlighting activates immediately.

```dux
import std from "std"

class Animal {
    private str name
    private int age

    fn __init__(str name, int age) {
        this.name = name
        this.age  = age
    }

    public fn speak() -> str {
        return f"My name is {this.name} and I am {this.age} years old."
    }
}

class Dog: Animal {
    override fn speak() -> str {
        return f"{super.speak()} Woof!"
    }
}

fn main() {
    auto dog = new Dog("Rex", 3)
    println(dog.speak())
}
```

### Snippet Shortcuts

| Prefix    | Expands to                            |
|-----------|---------------------------------------|
| `fn`      | Function definition with signature    |
| `class`   | Class definition with constructor     |
| `iface`   | Interface definition                  |
| `enum`    | Enum definition                       |
| `if`      | If / else block                       |
| `for`     | For-in loop                           |
| `while`   | While loop                            |
| `trycatch`| Try / catch block                     |
| `fstr`    | F-string template                     |
| `import`  | Import statement                      |

---

## Screenshots

> _Screenshots coming soon._

---

## Requirements

- Visual Studio Code `^1.70.0` **or** Cursor (any recent version)
- No additional runtime dependencies

---

## Extension Settings

This extension does not contribute any VS Code settings at this time.

---

## Known Issues

- Nested f-string expressions are highlighted but deep nesting (3+ levels) may not colour perfectly — this is a limitation of TextMate grammars.

---

## Contributing

Pull requests and bug reports are welcome at [github.com/vorjdux/dux-lang](https://github.com/vorjdux/dux-lang).

---

## License

[MIT](LICENSE)
