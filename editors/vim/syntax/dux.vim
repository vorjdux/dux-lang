" Vim syntax file
" Language:     Dux
" Maintainer:   Dux Language Project
" URL:          https://github.com/vorj/dux-lang
" File Types:   *.dux

if exists("b:current_syntax")
  finish
endif

" ============================================================
" Keywords
" ============================================================

" Control flow
syn keyword duxKeyword
      \ if else for while do switch match case default
      \ break continue return try catch throw in defer

" Declaration keywords
syn keyword duxStatement
      \ class interface enum fn namespace import from as
      \ extern unsafe async await new delete

" Modifier keywords
syn keyword duxModifier
      \ public private protected static const thread_local override

" Type keywords
syn keyword duxType
      \ void int long real double str bool list dict tuple
      \ object ptr auto

" Literal constants
syn keyword duxConstant
      \ true false null

" Word operators
syn keyword duxOperatorWord
      \ and or not

" Special identifiers
syn keyword duxSpecial
      \ this super

" Builtin functions
syn keyword duxBuiltin
      \ println print readline len range assert

" ============================================================
" Comments
" ============================================================

syn keyword duxTodo contained TODO FIXME NOTE HACK XXX

syn region duxComment
      \ start="//"
      \ end="$"
      \ keepend
      \ contains=duxTodo,@Spell

syn region duxCommentBlock
      \ start="/\*"
      \ end="\*/"
      \ fold
      \ contains=duxTodo,@Spell

" ============================================================
" Strings
" ============================================================

" Escape sequences (contained inside string regions)
syn match duxEscape contained /\\[\\'"abfnrtvx0-9]\|\\u[0-9a-fA-F]\{4}\|\\U[0-9a-fA-F]\{8}/

" Interpolation placeholder inside f-strings:  { expr }
syn region duxInterpolation
      \ contained
      \ start="{"
      \ end="}"
      \ contains=TOP
      \ keepend

" Double-quoted string:  "..."
syn region duxString
      \ start='"'
      \ skip=/\\./
      \ end='"'
      \ contains=duxEscape,@Spell

" Single-quoted string:  '...'
syn region duxString
      \ start="'"
      \ skip=/\\./
      \ end="'"
      \ contains=duxEscape,@Spell

" f-string:  f"..."  (interpolation allowed)
syn region duxFString
      \ start='f"'
      \ skip=/\\./
      \ end='"'
      \ contains=duxEscape,duxInterpolation,@Spell

" ============================================================
" Numbers
" ============================================================

" Hexadecimal:  0x[0-9a-fA-F]+
syn match duxNumber /\<0[xX][0-9a-fA-F]\+\>/

" Binary:  0b[01]+
syn match duxNumber /\<0[bB][01]\+\>/

" Float:  digits . digits (optional exponent)
syn match duxNumber /\<[0-9]\+\.[0-9]\+\([eE][+-]\?[0-9]\+\)\?\>/

" Float with exponent only:  digits e/E digits
syn match duxNumber /\<[0-9]\+[eE][+-]\?[0-9]\+\>/

" Integer:  plain decimal
syn match duxNumber /\<[0-9]\+\>/

" ============================================================
" Operators
" ============================================================

syn match duxOperator /[+\-*/%&|^~!<>=]=\?\|<<\|>>\|&&\|||\|::/
syn match duxOperator /=>/
syn match duxOperator /->/
syn match duxOperator /\.\.\./
syn match duxOperator /\.\./

" ============================================================
" Highlight links
" ============================================================

hi def link duxKeyword       Keyword
hi def link duxStatement     Statement
hi def link duxModifier      StorageClass
hi def link duxType          Type
hi def link duxConstant      Boolean
hi def link duxOperatorWord  Operator
hi def link duxSpecial       Special
hi def link duxBuiltin       Function
hi def link duxTodo          Todo
hi def link duxComment       Comment
hi def link duxCommentBlock  Comment
hi def link duxEscape        SpecialChar
hi def link duxInterpolation PreProc
hi def link duxString        String
hi def link duxFString       String
hi def link duxNumber        Number
hi def link duxOperator      Operator

let b:current_syntax = "dux"
