# Dux Language — Vim / Neovim Plugin

Syntax highlighting, filetype detection, indentation, and editor settings
for the [Dux programming language](https://github.com/vorj/dux-lang) in Vim
and Neovim.

## Features

- **Syntax highlighting** for all Dux keywords, types, literals, operators,
  comments (`//`, `/* */`), strings (`"..."`, `'...'`, `f"..."`), escape
  sequences, f-string interpolation blocks, and numeric literals (decimal,
  float, hex `0x…`, binary `0b…`).
- **Filetype detection** — automatically activates for `*.dux` files.
- **Indentation** — smart `DuxIndent()` function that:
  - increases indent after `{`
  - decreases indent before `}`
  - aligns `else` with its matching `if`
  - aligns `case` / `default` / `catch` with the enclosing block brace
  - handles continuation lines ending with `,`, operators, or `\`
- **Editor settings** — 4-space indentation, `//` comment string, correct
  `comments` option for block and line comments, sensible `formatoptions`.

## Installation

### vim-plug

```vim
" In your ~/.vimrc or ~/.config/nvim/init.vim
Plug 'vorj/dux-lang', { 'rtp': 'editors/vim' }
```

Then run `:PlugInstall`.

### lazy.nvim (Neovim)

```lua
-- In your lazy.nvim plugin spec (e.g. ~/.config/nvim/lua/plugins/dux.lua)
{
  "vorj/dux-lang",
  ft = "dux",
  config = function()
    vim.opt.rtp:append(vim.fn.stdpath("data") .. "/lazy/dux-lang/editors/vim")
  end,
}
```

### Packer (Neovim)

```lua
use {
  'vorj/dux-lang',
  rtp = 'editors/vim',
}
```

### Manual installation

#### Vim (with native packages, Vim 8+)

```sh
mkdir -p ~/.vim/pack/dux/start/dux-lang
cp -r editors/vim/* ~/.vim/pack/dux/start/dux-lang/
```

#### Neovim (with native packages)

```sh
mkdir -p ~/.local/share/nvim/site/pack/dux/start/dux-lang
cp -r editors/vim/* ~/.local/share/nvim/site/pack/dux/start/dux-lang/
```

#### Legacy (no package manager)

Copy the individual directories into your Vim runtime path:

```sh
cp editors/vim/syntax/dux.vim    ~/.vim/syntax/
cp editors/vim/ftdetect/dux.vim  ~/.vim/ftdetect/
cp editors/vim/ftplugin/dux.vim  ~/.vim/ftplugin/
cp editors/vim/indent/dux.vim    ~/.vim/indent/
```

## Neovim — Tree-sitter integration

For richer semantic highlighting in Neovim you can combine this plugin with
the Dux tree-sitter grammar. See
[`editors/tree-sitter/`](../tree-sitter/README.md) for setup instructions.

## File layout

```
editors/vim/
├── ftdetect/dux.vim   – filetype detection
├── ftplugin/dux.vim   – editor options (indent, comments, …)
├── indent/dux.vim     – DuxIndent() indentation function
└── syntax/dux.vim     – syntax highlighting rules
```
