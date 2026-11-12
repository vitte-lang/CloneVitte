" ============================================================================
" Vim compiler file for the Vitte programming language
" File: compiler/vitte.vim
" ============================================================================

if exists("current_compiler")
  finish
endif

let current_compiler = "vitte"

if exists(":CompilerSet") != 2
  command -nargs=* CompilerSet setlocal <args>
endif

" ============================================================================
" Compiler command
" ============================================================================

" Run the Vitte checker against the current source file.
"
" Quote the absolute filename for paths containing spaces or shell characters.
let &l:makeprg = shellescape(get(g:, 'vitte_binary', 'vitte')) . ' check --no-color ' . shellescape(expand('%:p'))

" ============================================================================
" Diagnostic formats
"
" Supported examples:
"
" file.vit:12:8: error: invalid expression
" file.vit:12:8: error[E1001]: invalid expression
" file.vit:12:8: warning: unused variable
" file.vit:12:8: warning[W2001]: unused variable
" file.vit:12:8: note: declared here
" file.vit:12:8: help: remove this declaration
"
" file.vit:12: error: missing expression
" file.vit:12: warning: unused import
"
" file.vit: error: invalid module
" file.vit: warning: deprecated declaration
"
" error[E1001]: invalid expression
" error: backend command rejected emitted source
" warning[W2001]: unused variable
" warning: experimental feature
" note: declaration originates here
" help: remove this declaration
"
" Rust-style continuation diagnostics are also accepted:
"
"   --> file.vit:12:8
"    |
"    | message
"    = note: additional information
"    = help: suggested action
"
" Unknown output is ignored instead of polluting the quickfix list.
" ============================================================================

let &l:errorformat =
      \ '%E%f:%l:%c: error[%*[^]]]: %m,' .
      \ '%E%f:%l:%c: error: %m,' .
      \ '%E%f:%l: error[%*[^]]]: %m,' .
      \ '%E%f:%l: error: %m,' .
      \ '%E%f: error[%*[^]]]: %m,' .
      \ '%E%f: error: %m,' .
      \ '%Eerror[%*[^]]]: %m,' .
      \ '%Eerror: %m,' .
      \ '%W%f:%l:%c: warning[%*[^]]]: %m,' .
      \ '%W%f:%l:%c: warning: %m,' .
      \ '%W%f:%l: warning[%*[^]]]: %m,' .
      \ '%W%f:%l: warning: %m,' .
      \ '%W%f: warning[%*[^]]]: %m,' .
      \ '%W%f: warning: %m,' .
      \ '%Wwarning[%*[^]]]: %m,' .
      \ '%Wwarning: %m,' .
      \ '%I%f:%l:%c: note: %m,' .
      \ '%I%f:%l: note: %m,' .
      \ '%I%f: note: %m,' .
      \ '%Inote: %m,' .
      \ '%I%f:%l:%c: help: %m,' .
      \ '%I%f:%l: help: %m,' .
      \ '%I%f: help: %m,' .
      \ '%Ihelp: %m,' .
      \ '%C%\s%#--> %f:%l:%c,' .
      \ '%C%\s%#| %m,' .
      \ '%C%\s%#= note: %m,' .
      \ '%C%\s%#= help: %m,' .
      \ '%C%\s%#= warning: %m,' .
      \ '%C%\s%#= error: %m,' .
      \ '%C%\s%#%m,' .
      \ '%-G%.%#'

" ============================================================================
" Compiler encoding
" ============================================================================

if exists("+makeencoding")
  CompilerSet makeencoding=utf-8
endif

" ============================================================================
" Quickfix behavior
" ============================================================================

" Use Vim's default temporary error file handling.

" Do not override 'shellpipe' here.
"
" 'shellpipe' is shell/platform dependent and the user's Vim configuration
" knows the correct redirection semantics. In particular, hard-coding:
"
"   2>&1|tee
"
" can alter exit status propagation and behaves differently between shells.

" ============================================================================
" Internal helpers
" ============================================================================

function! s:VitteMake(open_mode) abort
  " Save modified source before checking it.
  if &l:modified
    silent update
  endif

  let &l:makeprg = shellescape(get(g:, 'vitte_binary', 'vitte')) . ' check --no-color ' . shellescape(expand('%:p'))

  " Execute without automatically jumping to the first diagnostic.
  silent make!

  if a:open_mode ==# "window"
    cwindow
  elseif a:open_mode ==# "open"
    copen
  elseif a:open_mode ==# "close"
    cclose
  endif
endfunction

function! s:VitteNext() abort
  try
    cnext
  catch /^Vim\%((\a\+)\)\=:E553/
    echohl WarningMsg
    echomsg "No more Vitte diagnostics"
    echohl None
  catch /^Vim\%((\a\+)\)\=:E42/
    echohl WarningMsg
    echomsg "No Vitte diagnostics"
    echohl None
  endtry
endfunction

function! s:VittePrevious() abort
  try
    cprevious
  catch /^Vim\%((\a\+)\)\=:E553/
    echohl WarningMsg
    echomsg "No previous Vitte diagnostic"
    echohl None
  catch /^Vim\%((\a\+)\)\=:E42/
    echohl WarningMsg
    echomsg "No Vitte diagnostics"
    echohl None
  endtry
endfunction

function! s:VitteFirst() abort
  try
    cfirst
  catch /^Vim\%((\a\+)\)\=:E42/
    echohl WarningMsg
    echomsg "No Vitte diagnostics"
    echohl None
  endtry
endfunction

function! s:VitteLast() abort
  try
    clast
  catch /^Vim\%((\a\+)\)\=:E42/
    echohl WarningMsg
    echomsg "No Vitte diagnostics"
    echohl None
  endtry
endfunction

" ============================================================================
" Buffer-local commands
" ============================================================================

if !exists(":VitteCheck")
  command -buffer VitteCheck
        \ call <SID>VitteMake("window")
endif

if !exists(":VitteCheckOpen")
  command -buffer VitteCheckOpen
        \ call <SID>VitteMake("open")
endif

if !exists(":VitteCheckClose")
  command -buffer VitteCheckClose
        \ call <SID>VitteMake("close")
endif

if !exists(":VitteNextError")
  command -buffer VitteNextError
        \ call <SID>VitteNext()
endif

if !exists(":VittePreviousError")
  command -buffer VittePreviousError
        \ call <SID>VittePrevious()
endif

if !exists(":VitteFirstError")
  command -buffer VitteFirstError
        \ call <SID>VitteFirst()
endif

if !exists(":VitteLastError")
  command -buffer VitteLastError
        \ call <SID>VitteLast()
endif

if !exists(":VitteErrors")
  command -buffer VitteErrors copen
endif

if !exists(":VitteErrorsClose")
  command -buffer VitteErrorsClose cclose
endif

" ============================================================================
" Optional mappings
"
" No default key mappings are installed. This avoids overriding user mappings.
"
" Suggested configuration:
"
"   nnoremap <buffer> <leader>vc :VitteCheck<CR>
"   nnoremap <buffer> ]e :VitteNextError<CR>
"   nnoremap <buffer> [e :VittePreviousError<CR>
" ============================================================================

" ============================================================================
" End
" ============================================================================