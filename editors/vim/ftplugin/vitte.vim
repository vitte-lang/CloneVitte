" ============================================================================
" Vitte ftplugin for Vim
" Language: Vitte
" File: ftplugin/vitte.vim
" ============================================================================

if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

" ============================================================================
" Indentation
" ============================================================================

setlocal shiftwidth=2
setlocal tabstop=2
setlocal softtabstop=2
setlocal expandtab
setlocal autoindent
setlocal nosmartindent

" ============================================================================
" Comments
" ============================================================================

setlocal commentstring=//\ %s
setlocal comments=s1:/*,mb:*,ex:*/,://

" ============================================================================
" Vitte source extensions
" ============================================================================

" .vit   : primary Vitte source
" .vitl  : Vitte library / stdlib source
" .vitte : legacy compatibility source
setlocal suffixesadd=.vit,.vitl,.vitte

" ============================================================================
" Formatting
" ============================================================================

setlocal textwidth=0

setlocal formatoptions-=t
setlocal formatoptions+=c
setlocal formatoptions+=r
setlocal formatoptions+=o

" The bundled formatter works without a compiler fmt subcommand.
let b:vitte_editor = get(g:, 'vitte_editor', fnamemodify(expand('<sfile>:p'), ':h:h:h') . '/vitte-editor')
if !filereadable(b:vitte_editor)
  let b:vitte_editor = 'vitte-editor'
endif
let &l:formatprg = get(g:, 'vitte_formatprg', shellescape(b:vitte_editor) . ' format -')

" ============================================================================
" Compiler / diagnostics
" ============================================================================

" compiler/vitte.vim owns makeprg and errorformat.
"
" It provides:
"
"   :make
"   :VitteCheck
"   :VitteCheckOpen
"   :VitteCheckClose
"   :VitteErrors
"   :VitteErrorsClose
"   :VitteNextError
"   :VittePreviousError
"   :VitteFirstError
"   :VitteLastError

if exists(":compiler") == 2
  silent! compiler vitte
endif

" ============================================================================
" Navigation
" ============================================================================

" Search paths used by commands such as gf.
setlocal path+=.
setlocal path+=src
setlocal path+=stdlib

" Vitte identifiers use letters, digits and underscore.
setlocal iskeyword+=_

" ============================================================================
" Matching
" ============================================================================

" Standard pairs are already provided by Vim.
" Add generic/type angle brackets for Vitte.
if &l:matchpairs !~# '<:>'
  setlocal matchpairs+=<:>
endif

" ============================================================================
" Completion
" ============================================================================

" Vitte-specific omnifunc:
"
"   Ctrl-X Ctrl-O
"
" Implementation:
"
"   autoload/vitte/complete.vim
setlocal omnifunc=vitte#complete#Complete

" Generic Vim completion sources.
"
" .  current buffer
" w  other windows
" b  loaded buffers
" u  unloaded buffers
" t  tags
setlocal complete-=i
setlocal complete+=.,w,b,u,t

" Keep completion menu useful without forcing insertion.
if exists("+completeopt")
  setlocal completeopt=menu,menuone,noselect
endif

" ============================================================================
" Include handling
" ============================================================================

" Help Vim recognize common Vitte import lines when searching includes.
setlocal include=^\s*use\s\+

" ============================================================================
" Definition patterns
" ============================================================================

" Useful for commands based on Vim's definition search.
setlocal define=^\s*\%(proc\|form\|pick\|trait\|type\|opaque\|intrinsic\|macro\|test\)\s\+

" ============================================================================
" Keyword program
" ============================================================================

" K over a word can delegate to Vitte help when supported.
setlocal keywordprg=vitte\ help

" ============================================================================
" Buffer-local commands
" ============================================================================

command! -buffer VitteFormat call vitte#Format(0)
command! -buffer VitteFormatWrite call vitte#Format(1)
command! -buffer VitteBuild call vitte#Run('build')
command! -buffer VitteRun call vitte#Run('run')
command! -buffer VitteTest call vitte#Run('test')
command! -buffer VitteSymbols call vitte#Run('symbols')
command! -buffer VitteTags call vitte#Tags()

" ============================================================================
" Undo
" ============================================================================

let b:undo_ftplugin =
      \ "setlocal shiftwidth< " .
      \ "tabstop< " .
      \ "softtabstop< " .
      \ "expandtab< " .
      \ "autoindent< " .
      \ "smartindent< " .
      \ "smarttab< " .
      \ "commentstring< " .
      \ "comments< " .
      \ "suffixesadd< " .
      \ "textwidth< " .
      \ "formatoptions< " .
      \ "formatprg< " .
      \ "makeprg< " .
      \ "errorformat< " .
      \ "makeencoding< " .
      \ "makeef< " .
      \ "path< " .
      \ "iskeyword< " .
      \ "matchpairs< " .
      \ "omnifunc< " .
      \ "complete< " .
      \ "completeopt< " .
      \ "include< " .
      \ "define< " .
      \ "keywordprg< " .
      \ " | silent! delcommand VitteFormat" .
      \ " | silent! delcommand VitteFormatWrite"
let b:undo_ftplugin .= " | unlet! b:current_compiler"
for s:command in ['VitteBuild', 'VitteRun', 'VitteTest', 'VitteSymbols', 'VitteTags', 'VitteCheck', 'VitteCheckOpen', 'VitteCheckClose', 'VitteErrors', 'VitteErrorsClose', 'VitteNextError', 'VittePreviousError', 'VitteFirstError', 'VitteLastError']
  let b:undo_ftplugin .= ' | silent! delcommand ' . s:command
endfor
unlet s:command

let b:undo_ftplugin .= ' | unlet! b:vitte_editor'
