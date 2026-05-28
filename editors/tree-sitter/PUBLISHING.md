# Publishing tree-sitter-dux

This document covers how to publish `tree-sitter-dux` to npm and how to
register the grammar with the major editor ecosystems.

---

## 1. Publishing to npm

### One-time setup

```bash
# Log in to npm (create a free account at https://www.npmjs.com if needed)
npm login
```

### Publish a release

1. Bump the `version` field in `package.json` following semver.
2. Generate and build:

   ```bash
   npm run generate   # regenerates src/parser.c
   npm run build      # compiles the Node.js binding
   ```

3. Run the tests to confirm nothing regressed:

   ```bash
   npm test
   ```

4. Publish:

   ```bash
   npm publish --access public
   ```

The `prepublishOnly` script in `package.json` runs `generate` and `build`
automatically before every `npm publish`, so steps 2 and 4 can be combined.

### Version bumping policy

- **Patch** (`0.1.x`): Bug fixes to existing rules, no new nodes.
- **Minor** (`0.x.0`): New syntax nodes or query changes that are backwards
  compatible with existing highlight/locals queries.
- **Major** (`x.0.0`): Breaking changes to node names or the grammar structure
  that require updates in downstream queries.

---

## 2. Submitting to nvim-treesitter

[nvim-treesitter](https://github.com/nvim-treesitter/nvim-treesitter) maintains
an official list of parsers. To add Dux:

1. Fork `nvim-treesitter/nvim-treesitter` on GitHub.

2. Add an entry to `lua/nvim-treesitter/parsers.lua` (find the alphabetical
   position for `dux`):

   ```lua
   dux = {
     install_info = {
       url    = "https://github.com/dux-lang/dux-lang",
       path   = "editors/tree-sitter",
       files  = { "src/parser.c" },
       branch = "main",
     },
     maintainers = { "@dux-lang" },
   },
   ```

3. Add highlight queries to `queries/dux/highlights.scm` (and optionally
   `injections.scm`, `locals.scm`, `indents.scm`).

4. Open a pull request against the `master` branch of `nvim-treesitter`.
   The maintainers will review the grammar and query files.

5. Once merged, users can install via `:TSInstall dux`.

### Requirements checklist

- [ ] `src/parser.c` is committed and up-to-date with `grammar.js`.
- [ ] `highlights.scm` covers all node types visible in typical source files.
- [ ] The grammar compiles without warnings on Linux, macOS, and Windows.
- [ ] At least one corpus test exists in `test/corpus/`.

---

## 3. Registering with Emacs tree-sitter (`emacs-tree-sitter-langs`)

The [`tree-sitter-langs`](https://github.com/emacs-tree-sitter/tree-sitter-langs)
package bundles pre-compiled grammars for Emacs's older `tree-sitter` binding
(Emacs 28 + the `tree-sitter` package) and for Emacs 29+ `treesit`.

1. Fork `emacs-tree-sitter/tree-sitter-langs`.

2. Add a grammar entry in `langs/GRAMMARS.yml`:

   ```yaml
   dux:
     repo: https://github.com/dux-lang/dux-lang
     path: editors/tree-sitter
     branch: main
     # Optional ABI version pin
     # rev: <commit-sha>
   ```

3. Add highlight queries under `queries/dux/` (the format is the same
   `.scm` files used by nvim-treesitter).

4. Open a pull request. The maintainers will build the grammar and add it
   to the bundled release.

5. Once published, users simply `M-x package-install tree-sitter-langs` and
   the `dux` grammar is available.

---

## 4. Submitting to Helix

[Helix](https://helix-editor.com/) bundles grammars in its own repository.

1. Fork `helix-editor/helix`.

2. Add an entry to `languages.toml` (keep alphabetical order):

   ```toml
   [[language]]
   name = "dux"
   scope = "source.dux"
   injection-regex = "dux"
   file-types = ["dux"]
   comment-token = "//"
   indent = { tab-width = 4, unit = "    " }

   [[grammar]]
   name = "dux"
   source = { git = "https://github.com/dux-lang/dux-lang", subdir = "editors/tree-sitter", rev = "main" }
   ```

3. Add highlight queries to `runtime/queries/dux/highlights.scm` (and
   optionally `textobjects.scm`, `indents.scm`).

4. Open a pull request. Helix maintainers will review and build.

5. Once merged, Helix users get Dux syntax highlighting automatically on
   the next release.

---

## 5. Submitting to Zed

[Zed](https://zed.dev/) uses tree-sitter grammars via extensions. To add Dux:

1. Create a Zed extension repository (e.g. `zed-dux`) following the
   [Zed Extension API guide](https://zed.dev/docs/extensions/developing-extensions).

2. Add `extension.toml`:

   ```toml
   id = "dux"
   name = "Dux"
   version = "0.1.3"
   description = "Dux language support for Zed"
   authors = ["Dux Language Authors"]
   repository = "https://github.com/dux-lang/zed-dux"

   [grammars.dux]
   repository = "https://github.com/dux-lang/dux-lang"
   commit = "<pinned-commit-sha>"
   path = "editors/tree-sitter"
   ```

3. Add `languages/dux/highlights.scm` and `config.toml`:

   ```toml
   name = "Dux"
   grammar = "dux"
   path_suffixes = ["dux"]
   line_comment = "//"
   ```

4. Publish the extension to the Zed extension registry by opening a PR at
   `zed-industries/extensions` adding your extension to `extensions.toml`.

---

## Useful links

| Resource | URL |
|---|---|
| tree-sitter docs | https://tree-sitter.github.io/tree-sitter/ |
| nvim-treesitter contributing | https://github.com/nvim-treesitter/nvim-treesitter/blob/master/CONTRIBUTING.md |
| emacs-tree-sitter-langs | https://github.com/emacs-tree-sitter/tree-sitter-langs |
| Helix languages.toml | https://github.com/helix-editor/helix/blob/master/languages.toml |
| Zed extension API | https://zed.dev/docs/extensions/developing-extensions |
| npm publish docs | https://docs.npmjs.com/cli/v10/commands/npm-publish |
