;;; dux-mode.el --- Major mode for the Dux programming language  -*- lexical-binding: t; -*-

;; Copyright (C) 2024 Dux Language Authors

;; Author: Dux Language Authors
;; URL: https://github.com/dux-lang/dux-lang
;; Version: 0.1.3
;; Keywords: languages
;; Package-Requires: ((emacs "26.1"))

;; This file is not part of GNU Emacs.

;; This program is free software: you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.

;; This program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with this program.  If not, see <https://www.gnu.org/licenses/>.

;;; Commentary:

;; This package provides a major mode for editing Dux source files.
;;
;; Features:
;;   - Syntax highlighting for all keyword groups, types, literals, and builtins
;;   - Highlighting for class and function definitions
;;   - PascalCase type name recognition
;;   - f-string interpolation highlighting
;;   - Automatic indentation (4-space, brace-driven)
;;   - Comment support for // line comments and /* */ block comments
;;
;; Usage:
;;   Files with the `.dux' extension are automatically opened in dux-mode.
;;   You may also activate it manually with M-x dux-mode.

;;; Code:

(require 'rx)

;;; Customization

(defgroup dux nil
  "Major mode for the Dux programming language."
  :group 'languages
  :prefix "dux-")

(defcustom dux-indent-offset 4
  "Number of spaces per indentation level in Dux mode."
  :type 'integer
  :group 'dux)

;;; Syntax table

(defvar dux-mode-syntax-table
  (let ((table (make-syntax-table)))
    ;; Whitespace
    (modify-syntax-entry ?\n ">" table)

    ;; Line comments: //
    (modify-syntax-entry ?/ ". 124b" table)
    (modify-syntax-entry ?* ". 23" table)
    (modify-syntax-entry ?\n "> b" table)

    ;; String delimiters
    (modify-syntax-entry ?\" "\"" table)
    (modify-syntax-entry ?\' "\"" table)

    ;; Brackets
    (modify-syntax-entry ?\( "()" table)
    (modify-syntax-entry ?\) ")(" table)
    (modify-syntax-entry ?\[ "(]" table)
    (modify-syntax-entry ?\] ")[" table)
    (modify-syntax-entry ?\{ "(}" table)
    (modify-syntax-entry ?\} "){" table)

    ;; Underscore as word constituent
    (modify-syntax-entry ?_ "w" table)

    ;; Operators
    (modify-syntax-entry ?+ "." table)
    (modify-syntax-entry ?- "." table)
    (modify-syntax-entry ?= "." table)
    (modify-syntax-entry ?< "." table)
    (modify-syntax-entry ?> "." table)
    (modify-syntax-entry ?! "." table)
    (modify-syntax-entry ?& "." table)
    (modify-syntax-entry ?| "." table)
    (modify-syntax-entry ?% "." table)
    (modify-syntax-entry ?^ "." table)
    (modify-syntax-entry ?~ "." table)
    (modify-syntax-entry ?, "." table)
    (modify-syntax-entry ?\; "." table)
    (modify-syntax-entry ?: "." table)
    (modify-syntax-entry ?. "." table)
    table)
  "Syntax table for `dux-mode'.")

;;; Font-lock keywords

(defconst dux-control-flow-keywords
  '("if" "else" "for" "while" "do" "switch" "match" "case" "default"
    "break" "continue" "return" "try" "catch" "throw" "in" "defer")
  "Dux control flow keywords.")

(defconst dux-declaration-keywords
  '("class" "interface" "enum" "fn" "namespace" "import" "from" "as"
    "extern" "unsafe" "async" "await" "new" "delete")
  "Dux declaration keywords.")

(defconst dux-modifier-keywords
  '("public" "private" "protected" "static" "const" "thread_local" "override")
  "Dux modifier keywords.")

(defconst dux-type-keywords
  '("void" "int" "long" "real" "double" "str" "bool"
    "list" "dict" "tuple" "object" "ptr" "auto")
  "Dux built-in type keywords.")

(defconst dux-literal-constants
  '("true" "false" "null")
  "Dux literal constants.")

(defconst dux-word-operators
  '("and" "or" "not")
  "Dux word operators.")

(defconst dux-special-vars
  '("this" "super")
  "Dux special variables.")

(defconst dux-builtin-functions
  '("println" "print" "readline" "len" "range" "assert")
  "Dux built-in functions.")

(defun dux--build-keyword-regexp (keywords)
  "Build a word-boundary regexp matching any of KEYWORDS."
  (concat "\\_<" (regexp-opt keywords t) "\\_>"))

(defconst dux-font-lock-keywords
  `(
    ;; f-string interpolations: highlight {expr} inside f"..."
    ;; Must come before general string handling to catch content
    (,(rx (or (seq "f\"" (* (not (any "\"")))) (seq "f'" (* (not (any "'"))))))
     (0 font-lock-string-face))

    ;; f-string interpolation braces and contents
    (,(rx (seq (or "f\"" "f'")
               (* (not (any "{\"'")))
               (group "{" (* (not (any "}"))) "}")))
     (1 font-lock-variable-name-face t))

    ;; Class definitions: capture class name
    (,(rx symbol-start "class" (+ space) (group (seq upper (* (or word "_")))))
     (1 font-lock-type-face))

    ;; Interface definitions: capture interface name
    (,(rx symbol-start "interface" (+ space) (group (seq upper (* (or word "_")))))
     (1 font-lock-type-face))

    ;; Enum definitions: capture enum name
    (,(rx symbol-start "enum" (+ space) (group (seq upper (* (or word "_")))))
     (1 font-lock-type-face))

    ;; Function definitions: fn name(
    (,(rx symbol-start "fn" (+ space) (group (+ (or word "_"))) (* space) "(")
     (1 font-lock-function-name-face))

    ;; Function definitions: type name( where name is not a keyword
    ;; (for typed function declarations)
    (,(rx symbol-start (+ (or word "_")) (+ space)
          (group (+ (or word "_"))) (* space) "(")
     (1 font-lock-function-name-face))

    ;; PascalCase type references (user-defined types)
    (,(rx symbol-start (group (seq upper (+ (or word "_")))) symbol-end)
     (1 font-lock-type-face))

    ;; Control flow keywords
    (,(dux--build-keyword-regexp dux-control-flow-keywords)
     (0 font-lock-keyword-face))

    ;; Declaration keywords
    (,(dux--build-keyword-regexp dux-declaration-keywords)
     (0 font-lock-keyword-face))

    ;; Modifier keywords
    (,(dux--build-keyword-regexp dux-modifier-keywords)
     (0 font-lock-keyword-face))

    ;; Type keywords
    (,(dux--build-keyword-regexp dux-type-keywords)
     (0 font-lock-type-face))

    ;; Literal constants
    (,(dux--build-keyword-regexp dux-literal-constants)
     (0 font-lock-constant-face))

    ;; Word operators
    (,(dux--build-keyword-regexp dux-word-operators)
     (0 font-lock-builtin-face))

    ;; Special variables: this, super
    (,(dux--build-keyword-regexp dux-special-vars)
     (0 font-lock-variable-name-face))

    ;; Built-in functions
    (,(concat "\\_<" (regexp-opt dux-builtin-functions t) "\\_>" "[ \t]*(")
     (1 font-lock-builtin-face))

    ;; Hexadecimal integer literals
    (,(rx symbol-start "0" (or "x" "X") (+ hex-digit) symbol-end)
     (0 font-lock-constant-face))

    ;; Binary integer literals
    (,(rx symbol-start "0" (or "b" "B") (+ (any "01")) symbol-end)
     (0 font-lock-constant-face))

    ;; Floating-point literals
    (,(rx symbol-start (+ digit) "." (+ digit)
          (opt (or "e" "E") (opt (or "+" "-")) (+ digit))
          symbol-end)
     (0 font-lock-constant-face))

    ;; Integer literals
    (,(rx symbol-start (+ digit) symbol-end)
     (0 font-lock-constant-face))
    )
  "Font-lock keyword specification for `dux-mode'.")

;;; Indentation

(defun dux-calculate-indent ()
  "Calculate the indentation for the current line.

Indentation increases by `dux-indent-offset' after a line ending with `{',
and decreases by `dux-indent-offset' for lines beginning with `}'."
  (save-excursion
    (let ((current-line-start (line-beginning-position)))
      (beginning-of-line)
      ;; Check whether current line starts with }
      (let* ((closes-brace
              (looking-at "[ \t]*}"))
             (prev-indent
              (progn
                ;; Move to previous non-blank line
                (forward-line -1)
                (while (and (not (bobp))
                            (looking-at "[ \t]*$"))
                  (forward-line -1))
                ;; Get its indentation
                (current-indentation)))
             ;; Check whether previous non-blank line ends with {
             (prev-line-text
              (buffer-substring-no-properties
               (line-beginning-position)
               (line-end-position)))
             (opens-brace
              (string-match-p "{[ \t]*\\(//.*\\)?$" prev-line-text)))
        (cond
         ;; Current line closes a block: dedent from prev
         (closes-brace
          (max 0 (- prev-indent dux-indent-offset)))
         ;; Previous line opens a block: indent
         (opens-brace
          (+ prev-indent dux-indent-offset))
         ;; Otherwise keep same indent
         (t prev-indent))))))

(defun dux-indent-line ()
  "Indent the current line according to Dux indentation rules."
  (interactive)
  (let ((indent (dux-calculate-indent)))
    (save-excursion
      (beginning-of-line)
      (delete-horizontal-space)
      (indent-to indent))
    ;; If point was before the indentation, move it to the indent column
    (when (< (current-column) (current-indentation))
      (back-to-indentation))))

;;; Mode definition

;;;###autoload
(define-derived-mode dux-mode prog-mode "Dux"
  "Major mode for editing Dux programming language source files.

\\{dux-mode-map}"
  :syntax-table dux-mode-syntax-table
  :group 'dux

  ;; Font lock
  (setq-local font-lock-defaults
              '(dux-font-lock-keywords
                nil   ; KEYWORDS-ONLY: nil = also fontify strings/comments
                nil   ; CASE-FOLD
                nil   ; SYNTAX-ALIST
                nil)) ; SYNTAX-BEGIN

  ;; Indentation
  (setq-local indent-line-function #'dux-indent-line)
  (setq-local indent-tabs-mode nil)
  (setq-local tab-width dux-indent-offset)

  ;; Comments
  (setq-local comment-start "// ")
  (setq-local comment-end "")
  (setq-local comment-start-skip "\\(//+\\|/\\*+\\)\\s-*")
  (setq-local comment-end-skip "\\s-*\\(\\*+/\\)")

  ;; Paragraph / filling
  (setq-local paragraph-start (concat "\\s-*$\\|" page-delimiter))
  (setq-local paragraph-separate paragraph-start)

  ;; Electric features
  (setq-local electric-indent-chars (append "{}()[]" electric-indent-chars))

  ;; Imenu: recognize fn and class definitions
  (setq-local imenu-generic-expression
              '(("Functions" "\\bfn\\s-+\\(\\w+\\)\\s-*(" 1)
                ("Classes"   "\\bclass\\s-+\\([A-Z]\\w*\\)" 1)
                ("Interfaces" "\\binterface\\s-+\\([A-Z]\\w*\\)" 1)
                ("Enums"     "\\benum\\s-+\\([A-Z]\\w*\\)" 1))))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.dux\\'" . dux-mode))

(provide 'dux-mode)

;;; dux-mode.el ends here
