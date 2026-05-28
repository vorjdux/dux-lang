# Publishing the Dux Vim Plugin

## Best practice: a dedicated plugin repository

Package managers (vim-plug, Packer, lazy.nvim) work best when a Vim plugin
lives at the **root** of a repository. The recommended approach is to keep a
separate, dedicated repo:

```
dux-lang.vim/           ← repository root
├── ftdetect/dux.vim
├── ftplugin/dux.vim
├── indent/dux.vim
├── syntax/dux.vim
├── README.md
└── PUBLISHING.md
```

To create it, simply copy the contents of `editors/vim/` into a new
repository named **`dux-lang.vim`**:

```sh
git init dux-lang.vim
cp -r editors/vim/* dux-lang.vim/
cd dux-lang.vim
git add .
git commit -m "Initial release of dux-lang.vim"
```

Push to GitHub under a name such as `vorj/dux-lang.vim`.

---

## Hosting on GitHub

Once the plugin is in its own GitHub repository users can install it with any
major plugin manager.

### vim-plug

```vim
Plug 'vorj/dux-lang.vim'
```

### Packer (Neovim)

```lua
use 'vorj/dux-lang.vim'
```

### lazy.nvim (Neovim)

```lua
{ "vorj/dux-lang.vim", ft = "dux" }
```

### Manual / native packages

```sh
# Vim 8+
git clone https://github.com/vorj/dux-lang.vim \
    ~/.vim/pack/dux/start/dux-lang.vim

# Neovim
git clone https://github.com/vorj/dux-lang.vim \
    ~/.local/share/nvim/site/pack/dux/start/dux-lang.vim
```

---

## Publishing to vim.org (scripts.vim.org)

[vim.org](https://www.vim.org/scripts/) is the traditional distribution
channel. Follow these steps:

### 1. Create a vim.org account

1. Go to <https://www.vim.org/login.php> and click **register**.
2. Fill in username, e-mail, and password, then confirm via the link in the
   activation e-mail.

### 2. Prepare the archive

Only the four runtime directories are needed. From the root of
`dux-lang.vim/`:

```sh
zip -r dux-lang.vim.zip ftdetect/ ftplugin/ indent/ syntax/
```

### 3. Upload the script

1. Log in at <https://www.vim.org/login.php>.
2. Go to **Scripts → Add a new script** (direct URL:
   <https://www.vim.org/scripts/add_script.php>).
3. Fill in the form:

   | Field | Value |
   |---|---|
   | Script name | dux-lang.vim |
   | Script type | syntax / indent / ftplugin |
   | Summary | Vim plugin for the Dux programming language |
   | Description | (copy from README.md) |
   | Vim version | 7.0+ (or Neovim) |
   | Script file | Upload `dux-lang.vim.zip` |

4. Click **Upload**. The script receives a permanent URL like
   `https://www.vim.org/scripts/script.php?script_id=XXXXX`.

### 4. Releasing updates

For each new version:

1. Bump a version comment at the top of the main `syntax/dux.vim`.
2. Re-create the zip with the same name.
3. On vim.org go to your script page and click **Add new version**.

---

## Neovim — Tree-sitter integration

For Neovim users who prefer semantic, incremental highlighting, a Tree-sitter
grammar for Dux is available in [`editors/tree-sitter/`](../tree-sitter/).
The Vim regex-based plugin and the Tree-sitter grammar can coexist: the
ftdetect and ftplugin files are still useful (indentation, comment settings)
even when Tree-sitter handles highlighting.

To advertise the integration, add this to the plugin's README:

```md
## Neovim Tree-sitter

Install [nvim-treesitter](https://github.com/nvim-treesitter/nvim-treesitter)
and the Dux grammar:

    :TSInstall dux

Tree-sitter highlighting will automatically take priority over the regex
syntax once the grammar is compiled.
```
