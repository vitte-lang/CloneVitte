" ============================================================================
" Vim indent file for the Vitte programming language
" Language: Vitte
" File: indent/vitte.vim
" ============================================================================

if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

setlocal indentexpr=GetVitteIndent()
setlocal indentkeys=0{,0},0),0],:,!^F,o,O,e,
      \=else

let b:undo_indent = "setlocal indentexpr< indentkeys<"

function! GetVitteIndent() abort
  let l:stack = []
  let l:comment = 0
  let l:quote = ''
  for l:n in range(1, v:lnum - 1)
    let l:line = getline(l:n)
    let l:i = 0
    while l:i < strlen(l:line)
      let l:c = l:line[l:i]
      let l:pair = strpart(l:line, l:i, 2)
      if l:comment
        if l:pair ==# '*/'
          let l:comment -= 1
          let l:i += 1
        elseif l:pair ==# '/*'
          let l:comment += 1
          let l:i += 1
        endif
      elseif l:quote !=# ''
        if l:c ==# '\'
          let l:i += 1
        elseif l:c ==# l:quote
          let l:quote = ''
        endif
      elseif l:pair ==# '//'
        break
      elseif l:pair ==# '/*'
        let l:comment = 1
        let l:i += 1
      elseif l:c ==# '"' || l:c ==# "'"
        let l:quote = l:c
      elseif l:c =~# '[{([]'
        call add(l:stack, l:n)
      elseif l:c =~# '[]})]' && !empty(l:stack)
        call remove(l:stack, -1)
      endif
      let l:i += 1
    endwhile
  endfor
  if empty(l:stack)
    return 0
  endif
  return indent(l:stack[-1]) + (getline(v:lnum) =~# '^\s*[]})]' ? 0 : shiftwidth())
endfunction
