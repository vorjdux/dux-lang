" Vim filetype plugin for Dux
" Language:   Dux
" Maintainer: Dux Language Project

if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

" -------------------------------------------------------
" Indentation
" -------------------------------------------------------
setlocal expandtab
setlocal tabstop=4
setlocal shiftwidth=4
setlocal softtabstop=4

" -------------------------------------------------------
" Comments
" -------------------------------------------------------
" cs  - block comment  /* */
" :// - line  comment  //
setlocal comments=sO:*\ -,mO:*\ \ ,exO:*/,s1:/*,mb:*,ex:*/,://
setlocal commentstring=//\ %s

" -------------------------------------------------------
" Format options
" -------------------------------------------------------
"   c - auto-wrap comments at textwidth
"   r - insert comment leader after <Enter> in insert mode
"   o - insert comment leader after 'o'/'O' in normal mode
"   q - allow formatting of comments with gq
"   l - long lines not broken in insert mode
"   j - remove comment leader when joining lines (Vim 7.4+)
setlocal formatoptions+=crqjl
setlocal formatoptions-=t

" -------------------------------------------------------
" Undo all settings when the filetype changes
" -------------------------------------------------------
let b:undo_ftplugin =
      \ "setlocal expandtab< tabstop< shiftwidth< softtabstop<" .
      \ " | setlocal comments< commentstring<" .
      \ " | setlocal formatoptions<"
