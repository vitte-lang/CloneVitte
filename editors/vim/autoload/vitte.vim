" Run an explicitly configured formatter without replacing source on failure.
function! vitte#Format(write) abort
  let l:command = get(g:, 'vitte_formatprg', &l:formatprg)
  if empty(l:command)
    echoerr 'Set g:vitte_formatprg to an installed formatter; vitte has no fmt command'
    return
  endif
  let l:result = system(l:command, join(getline(1, '$'), "\n") . "\n")
  if v:shell_error
    echoerr 'Vitte formatter failed: ' . l:result
    return
  endif
  let l:lines = split(l:result, "\n", 1)
  if !empty(l:lines) && l:lines[-1] ==# ''
    call remove(l:lines, -1)
  endif
  let l:view = winsaveview()
  call setline(1, empty(l:lines) ? [''] : l:lines)
  if line('$') > max([1, len(l:lines)])
    execute (len(l:lines) + 1) . ',$delete _'
  endif
  call winrestview(l:view)
  if a:write
    update
  endif
endfunction

function! vitte#Run(command) abort
  if empty(expand('%:p'))
    echoerr 'Save this buffer to a file first'
    return
  endif
  update
  let l:cmd = shellescape(b:vitte_editor) . ' --compiler ' . shellescape(get(g:, 'vitte_binary', 'vitte')) . ' ' . a:command . ' ' . shellescape(expand('%:p'))
  execute '!' . l:cmd
endfunction

function! vitte#Tags() abort
  let l:root = getcwd()
  let l:output = l:root . '/tags'
  let l:result = system(shellescape(b:vitte_editor) . ' tags ' . shellescape(l:root) . ' --output ' . shellescape(l:output))
  if v:shell_error
    echoerr l:result
  else
    echomsg l:result
  endif
endfunction
