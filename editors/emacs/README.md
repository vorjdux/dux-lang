# dux-mode — Emacs major mode for Dux

`dux-mode` provides syntax highlighting, indentation, and comment support for
the [Dux programming language](https://github.com/dux-lang/dux-lang) inside
GNU Emacs (26.1 or later).

## Features

- **Syntax highlighting** for all keyword groups (control flow, declarations,
  modifiers, types), literal constants, word operators, built-in functions,
  class/function definition names, PascalCase type references, numeric
  literals (decimal, hex `0x…`, binary `0b…`, float), and f-string
  interpolation placeholders (`{expr}`).
- **Automatic indentation** — 4-space, brace-driven: indentation increases
  after a line ending with `{` and decreases on lines beginning with `}`.
- **Comment support** — `//` line comments (`M-;`, `C-x C-;`) and `/* */`
  block comments recognised by Emacs's comment commands.
- **Imenu** integration — jump to function, class, interface, and enum
  definitions with `M-x imenu`.
- **Auto-mode** — `.dux` files open in `dux-mode` automatically.

## Installation

### Manual

1. Download `dux-mode.el` and place it somewhere on your `load-path`,
   e.g. `~/.emacs.d/lisp/`.

2. Add to your Emacs configuration:

   ```emacs-lisp
   (add-to-list 'load-path "~/.emacs.d/lisp/")
   (require 'dux-mode)
   ```

### use-package (from a local path)

```emacs-lisp
(use-package dux-mode
  :load-path "~/.emacs.d/lisp/"
  :mode "\\.dux\\'")
```

### straight.el (from Git)

```emacs-lisp
(use-package dux-mode
  :straight (:host github :repo "dux-lang/dux-lang"
             :files ("editors/emacs/dux-mode.el"))
  :mode "\\.dux\\'")
```

### MELPA (once published)

Once the package is available on MELPA, install it with:

```
M-x package-install RET dux-mode RET
```

Then add to your configuration:

```emacs-lisp
(use-package dux-mode
  :ensure t
  :mode "\\.dux\\'")
```

## Configuration

The indentation offset defaults to 4 spaces and can be changed:

```emacs-lisp
(setq dux-indent-offset 4)
```

Or per-project via a `.dir-locals.el` file:

```emacs-lisp
((dux-mode . ((dux-indent-offset . 4))))
```

## Compatibility

Requires GNU Emacs 26.1 or later. Tested up to Emacs 30.x.
