# Publishing dux-mode to MELPA

This document describes how to submit `dux-mode` to
[MELPA](https://melpa.org/) and how to maintain it over time.

## Prerequisites

- The package must be hosted in a public Git repository (GitHub, GitLab, etc.).
- `dux-mode.el` must have a valid package header — Version, Keywords,
  Package-Requires, URL, and a `(provide 'dux-mode)` footer.
- A MELPA recipe file must be added via a pull request to the
  [MELPA repository](https://github.com/melpa/melpa).

## Step-by-step submission

### 1. Ensure the package header is valid

The top of `dux-mode.el` must contain lines like:

```emacs-lisp
;; Version: 0.1.3
;; Keywords: languages
;; Package-Requires: ((emacs "26.1"))
;; URL: https://github.com/dux-lang/dux-lang
```

Run `M-x package-lint-current-buffer` (install `package-lint` first) to
catch common header issues before submitting.

### 2. Fork the MELPA repository

```bash
# Fork via the GitHub UI, then:
git clone https://github.com/<your-username>/melpa.git
cd melpa
git remote add upstream https://github.com/melpa/melpa.git
```

### 3. Create the recipe file

Add a new file at `recipes/dux-mode` (no extension) with the following
content, adjusted to match your repository layout:

```emacs-lisp
(dux-mode
 :fetcher github
 :repo "dux-lang/dux-lang"
 :files ("editors/emacs/dux-mode.el"))
```

If the package is in its own dedicated repository the recipe simplifies to:

```emacs-lisp
(dux-mode
 :fetcher github
 :repo "dux-lang/dux-mode")
```

Supported `:fetcher` values are `github`, `gitlab`, `codeberg`, and `git`
(for arbitrary URLs).

### 4. Test the recipe locally

From inside the cloned MELPA repo:

```bash
make recipes/dux-mode
```

This fetches and builds the package and reports any errors.

### 5. Open a pull request

```bash
git checkout -b add-dux-mode
git add recipes/dux-mode
git commit -m "Add dux-mode recipe"
git push origin add-dux-mode
```

Open a PR against `melpa/melpa`. Follow the
[MELPA contribution guidelines](https://github.com/melpa/melpa/blob/master/CONTRIBUTING.org)
and fill in the PR checklist.

The MELPA maintainers will review the recipe and the package. Once merged,
`dux-mode` will appear on https://melpa.org within a few hours.

## Version bumping

MELPA tracks the latest commit on the default branch by default
(`:branch` can pin to a specific branch). When you push a new commit,
MELPA rebuilds and re-publishes the package automatically.

For a stable `MELPA Stable` release:

1. Bump the `Version:` header in `dux-mode.el`.
2. Create a Git tag that matches the version number:

   ```bash
   git tag -a 0.1.4 -m "Release 0.1.4"
   git push origin 0.1.4
   ```

MELPA Stable picks up tagged releases only.

## Keeping the recipe up to date

If the file moves within the repository, update both the `:files` list in
`recipes/dux-mode` and open a new PR to the MELPA repo. Changing only the
source repo requires updating `:repo` (or `:url` for the `git` fetcher).

## Useful resources

- MELPA contribution guide: https://github.com/melpa/melpa/blob/master/CONTRIBUTING.org
- `package-lint`: https://github.com/purcell/package-lint
- Emacs package authoring guide: https://www.gnu.org/software/emacs/manual/html_node/elisp/Packaging.html
