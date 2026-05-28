# Dux Editor Support

Syntax highlighting and language tooling for the Dux programming language,
organised by editor.

| Directory | Editors | What's included |
|-----------|---------|-----------------|
| [`vscode/`](vscode/) | VS Code, Cursor | TextMate grammar, language config, snippets |
| [`vim/`](vim/) | Vim, Neovim (traditional) | syntax, ftdetect, ftplugin, indent |
| [`emacs/`](emacs/) | Emacs 26+ | `dux-mode.el` major mode |
| [`tree-sitter/`](tree-sitter/) | Neovim (nvim-treesitter), Emacs 29+, Helix, Zed | Full Tree-sitter grammar |

## Quick start

### VS Code / Cursor

```bash
cd editors/vscode
npm install
npx vsce package        # produces dux-lang-x.y.z.vsix
```

Then **Extensions → Install from VSIX…** in VS Code or Cursor.

### Vim / Neovim (vim-plug)

```vim
Plug 'vorjdux/dux-lang', { 'rtp': 'editors/vim' }
```

### Neovim (lazy.nvim)

```lua
{ 'vorjdux/dux-lang', config = false, ft = 'dux',
  init = function()
    vim.opt.rtp:append(vim.fn.stdpath('data') .. '/lazy/dux-lang/editors/vim')
  end }
```

### Emacs

```elisp
(load "/path/to/dux-lang/editors/emacs/dux-mode.el")
```

Or with `use-package` + `straight.el`:

```elisp
(use-package dux-mode
  :straight (:host github :repo "vorjdux/dux-lang"
             :files ("editors/emacs/dux-mode.el")))
```

### Tree-sitter (Neovim nvim-treesitter)

```lua
-- In your nvim-treesitter setup, add a local install:
require('nvim-treesitter.parsers').get_parser_configs().dux = {
  install_info = {
    url  = 'https://github.com/vorjdux/dux-lang',
    files = { 'editors/tree-sitter/src/parser.c' },
    branch = 'master',
    generate_requires_npm = true,
    requires_generate_from_grammar = true,
  },
  filetype = 'dux',
}
```

## Publishing

Each subdirectory contains a `PUBLISHING.md` with step-by-step instructions
for the relevant marketplace or registry:

- `vscode/PUBLISHING.md` — VS Code Marketplace + OpenVSX (for Cursor)
- `vim/PUBLISHING.md` — vim.org + GitHub (vim-plug / lazy.nvim / Packer)
- `emacs/PUBLISHING.md` — MELPA submission
- `tree-sitter/PUBLISHING.md` — npm + nvim-treesitter + Emacs/Helix/Zed

## File extension

Dux source files use the `.dux` extension. All editor integrations register
this extension automatically.
