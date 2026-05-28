" Vim indent file for Dux
" Language:   Dux
" Maintainer: Dux Language Project

if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

setlocal indentexpr=DuxIndent(v:lnum)
setlocal indentkeys=0{,0},0),!^F,o,O,e,=else,=case,=default,=catch

" Only define the function once per Vim session.
if exists("*DuxIndent")
  finish
endif

" ============================================================
" DuxIndent(lnum) -- returns the indent for line {lnum}
" ============================================================
function! DuxIndent(lnum)
  " Lines 0 and 1 need no indentation calculation.
  if a:lnum <= 1
    return 0
  endif

  " Find the previous non-blank line.
  let l:prev_lnum = prevnonblank(a:lnum - 1)
  if l:prev_lnum == 0
    return 0
  endif

  let l:prev_line = getline(l:prev_lnum)
  let l:curr_line = getline(a:lnum)
  let l:sw       = shiftwidth()

  " Base indent: use the indent of the previous non-blank line.
  let l:ind = indent(l:prev_lnum)

  " --------------------------------------------------------
  " Increase indent after lines that open a block
  " A line "opens" a block if it ends with { (ignoring
  " trailing whitespace and line comments).
  " --------------------------------------------------------
  let l:prev_stripped = substitute(l:prev_line, '//.*$', '', '')
  let l:prev_stripped = substitute(l:prev_stripped, '\s*$', '', '')

  if l:prev_stripped =~ '{\s*$'
    let l:ind += l:sw
  endif

  " --------------------------------------------------------
  " Handle continuation lines.
  " If the previous line ends with an operator, comma, or
  " backslash, it is a continuation -- add one extra level
  " only when the line before that was NOT already a
  " continuation itself.
  " --------------------------------------------------------
  if l:prev_stripped =~ '[,+\-*/%&|^~<>=]\s*$\|\\$'
    let l:preprev_lnum = prevnonblank(l:prev_lnum - 1)
    if l:preprev_lnum > 0
      let l:preprev = substitute(getline(l:preprev_lnum), '//.*$', '', '')
      let l:preprev = substitute(l:preprev, '\s*$', '', '')
      if l:preprev !~ '[,+\-*/%&|^~<>=]\s*$\|\\$'
        let l:ind += l:sw
      endif
    endif
  endif

  " --------------------------------------------------------
  " Decrease indent for lines that close a block.
  " A line "closes" a block if its first non-whitespace
  " character is }.
  " --------------------------------------------------------
  if l:curr_line =~ '^\s*}'
    let l:ind -= l:sw
  endif

  " --------------------------------------------------------
  " Keep 'case', 'default', and 'catch' at the same level
  " as the enclosing 'switch' / 'try' block brace.
  " We detect this by checking whether the current line
  " starts with one of those keywords and the previous
  " non-blank, non-case line ended with '{'.
  " --------------------------------------------------------
  if l:curr_line =~ '^\s*\(case\|default\|catch\)\>'
    " Walk back to find the matching opening brace level.
    let l:search = l:prev_lnum
    while l:search > 1
      let l:sline = substitute(getline(l:search), '//.*$', '', '')
      let l:sline = substitute(l:sline, '\s*$', '', '')
      if l:sline =~ '{\s*$'
        " Align with the indent of that opening line.
        let l:ind = indent(l:search)
        break
      endif
      let l:search -= 1
    endwhile
  endif

  " --------------------------------------------------------
  " 'else' should align with its 'if'.
  " --------------------------------------------------------
  if l:curr_line =~ '^\s*else\>'
    " Search backward for the matching 'if' at the same depth.
    let l:depth  = 0
    let l:search = l:prev_lnum
    while l:search >= 1
      let l:sline = getline(l:search)
      " Count braces to track nesting depth.
      let l:depth -= len(substitute(l:sline, '[^{]', '', 'g'))
      let l:depth += len(substitute(l:sline, '[^}]', '', 'g'))
      if l:depth <= 0 && l:sline =~ '^\s*if\>'
        let l:ind = indent(l:search)
        break
      endif
      let l:search -= 1
    endwhile
  endif

  " Never return a negative indent.
  return max([0, l:ind])
endfunction
