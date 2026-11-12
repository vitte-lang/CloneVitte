set nocompatible
execute 'set runtimepath^=' . fnameescape($VITTE_EDITOR_ROOT . '/vim')
filetype plugin indent on
syntax on
execute 'edit ' . fnameescape($VITTE_EDITOR_TMP . '/file with spaces.vit')
call setline(1, ['proc main() {', 'if true {', 'let x = "https://example.com/{";', '} else {', 'call(', '1,', '2', ');', '}', '}'])
normal! gg=G
call assert_equal([0,2,4,2,4,6,6,4,2,0], map(range(1,10), 'indent(v:val)'))
call assert_equal('vitte', &filetype)
call assert_true(index(vitte#complete#Complete(0, 'pro'), 'proc') >= 0)
call assert_match('check --no-color', &l:makeprg)
call assert_match('file with spaces.vit', &l:makeprg)
let diagnostics = ['error[E0203]: expected expression', '  phase: parser | category: syntax', '  --> /tmp/bad.vit:2:10', '  |', '2 | let x = ;', '  |         ^ found ;']
let parsed = getqflist({'lines': diagnostics, 'efm': &l:errorformat}).items
call assert_equal(1, len(parsed))
call assert_equal(2, parsed[0].lnum)
call assert_equal(10, parsed[0].col)
call assert_equal('E', parsed[0].type)
let g:vitte_formatprg = 'cat'
VitteFormat
let before = getline(1, '$')
let g:vitte_formatprg = 'false'
silent! VitteFormat
call assert_equal(before, getline(1, '$'))
if !empty(v:errors)
  call writefile(v:errors, $VITTE_EDITOR_TMP . '/vim-errors')
  cquit
endif
qa!
