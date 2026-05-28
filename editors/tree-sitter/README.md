# tree-sitter-dux

[Tree-sitter](https://tree-sitter.github.io/) grammar for the
[Dux programming language](https://github.com/dux-lang/dux-lang).

Tree-sitter produces a concrete syntax tree (CST) that is fast, error-tolerant,
and language-agnostic. This grammar is consumed directly by:

| Editor / tool | Integration |
|---|---|
| **Neovim** | [nvim-treesitter](https://github.com/nvim-treesitter/nvim-treesitter) |
| **Emacs 29+** | Built-in `treesit` library |
| **Helix** | First-class tree-sitter support |
| **Zed** | First-class tree-sitter support |
| **GitHub** | Syntax highlighting and code navigation |

## Prerequisites

- Node.js 16+
- `tree-sitter-cli` 0.20+ (`npm install -g tree-sitter-cli`)
- A C compiler (gcc / clang / MSVC)
- `node-gyp` (`npm install -g node-gyp`)

## Building from source

```bash
# Install dependencies
npm install

# Generate src/parser.c from grammar.js
npm run generate

# Compile the native Node.js binding
npm run build
```

After a successful build, `build/Release/tree_sitter_dux_binding.node` will be
present.

### Run the built-in tests

```bash
npm test
```

Test cases live in `test/corpus/` as `.txt` files in the standard tree-sitter
corpus format.

## Installing in Neovim (nvim-treesitter)

### Option A — wait for the official parser

Once `dux` is merged into nvim-treesitter's parser list, installation is one
line inside your Neovim config:

```lua
require("nvim-treesitter.configs").setup {
  ensure_installed = { "dux" },
  highlight = { enable = true },
}
```

### Option B — local / custom install (before official merge)

1. Build the parser as described above.

2. Copy `src/parser.c` (and `src/tree_sitter/`) to the nvim-treesitter parser
   directory, or register a custom parser path:

   ```lua
   local parser_config = require("nvim-treesitter.parsers").get_parser_configs()
   parser_config.dux = {
     install_info = {
       url  = "https://github.com/dux-lang/dux-lang",
       -- path inside the repo to the grammar directory
       path = "editors/tree-sitter",
       files = { "src/parser.c" },
       branch = "main",
       generate_requires_npm = false,
       requires_generate_from_grammar = false,
     },
     filetype = "dux",
   }

   vim.filetype.add({ extension = { dux = "dux" } })
   ```

3. Then run `:TSInstall dux` from within Neovim.

4. Place highlight queries at
   `~/.config/nvim/queries/dux/highlights.scm` (or inside your plugin's
   `queries/dux/` directory).

## Using with Emacs 29+ (`treesit`)

Emacs 29 ships with a built-in tree-sitter binding (`treesit`). To use this
grammar:

1. Build the shared library:

   ```bash
   npm run generate
   # Compile to a .so / .dylib / .dll that Emacs can load
   gcc -shared -fPIC -o libtree-sitter-dux.so src/parser.c \
       -I./node_modules/tree-sitter/lib/include
   ```

   On macOS replace `.so` with `.dylib`; on Windows use `.dll`.

2. Place the shared library in a directory on `treesit-extra-load-path`:

   ```emacs-lisp
   (add-to-list 'treesit-extra-load-path "~/.emacs.d/tree-sitter/")
   ```

3. Verify Emacs can load it:

   ```emacs-lisp
   (treesit-language-available-p 'dux)  ; should return t
   ```

4. Enable the `dux-ts-mode` major mode (once published) for `.dux` files:

   ```emacs-lisp
   (add-to-list 'auto-mode-alist '("\\.dux\\'" . dux-ts-mode))
   ```

## Queries

Highlight, injection, and locals query files are expected at:

```
queries/
  highlights.scm
  injections.scm
  locals.scm
```

These are referenced in `package.json` under the `tree-sitter` key and are
loaded automatically by editors that support them.

## License

MIT — see the root `LICENSE` file.
