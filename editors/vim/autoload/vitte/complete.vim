" Keyword and current-buffer identifier completion; no server required.
function! vitte#complete#Complete(findstart, base) abort
  if a:findstart
    let l:start = col('.') - 1
    let l:line = getline('.')
    while l:start > 0 && l:line[l:start - 1] =~# '[A-Za-z0-9_]'
      let l:start -= 1
    endwhile
    return l:start
  endif
  let l:words = split('space use export const static type opaque form pick trait impl proc extern intrinsic macro test pub async unsafe comptime where requires ensures let mut set return give break continue defer assert if else while loop for in match asm ref move await not and or self as true false null void never unit bool char rune str string bytes cstr int i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize intptr uintptr f16 f32 f64 f128 c_char c_int c_uint c_long c_ulong c_void')
  for l:line in getline(1, '$')
    call extend(l:words, split(substitute(l:line, '[^A-Za-z0-9_]', ' ', 'g')))
  endfor
  return filter(uniq(sort(l:words)), 'stridx(v:val, a:base) == 0')
endfunction
