" A complete reference colorscheme with standard groups and Vitte links.
hi clear
if exists('syntax_on')
  syntax reset
endif
let g:colors_name = 'vitte_reference'
set background=dark
hi Normal guifg=#d8dee9 guibg=#20242c ctermfg=253 ctermbg=235
hi Comment guifg=#8793a5 ctermfg=103 gui=italic
hi Keyword guifg=#c792ea ctermfg=176
hi Statement guifg=#c792ea ctermfg=176
hi StorageClass guifg=#c792ea ctermfg=176
hi Type guifg=#82aaff ctermfg=111
hi Function guifg=#82d9c8 ctermfg=116
hi String guifg=#c3e88d ctermfg=150
hi Character guifg=#c3e88d ctermfg=150
hi Number guifg=#f78c6c ctermfg=209
hi Float guifg=#f78c6c ctermfg=209
hi Constant guifg=#f78c6c ctermfg=209
hi Boolean guifg=#f78c6c ctermfg=209
hi Identifier guifg=#d8dee9 ctermfg=253
hi Operator guifg=#89ddff ctermfg=117
hi Delimiter guifg=#a6accd ctermfg=146
hi SpecialChar guifg=#ffcb6b ctermfg=221
hi Error guifg=#ffffff guibg=#9f3030 ctermfg=15 ctermbg=124
hi Todo guifg=#20242c guibg=#ffcb6b ctermfg=235 ctermbg=221
hi LineNr guifg=#697180 ctermfg=242
hi CursorLine guibg=#292e38 ctermbg=236
hi Visual guibg=#3e4b63 ctermbg=60
hi Search guifg=#20242c guibg=#ffcb6b ctermfg=235 ctermbg=221
hi Pmenu guifg=#d8dee9 guibg=#303745 ctermfg=253 ctermbg=237
hi PmenuSel guifg=#20242c guibg=#82aaff ctermfg=235 ctermbg=111
hi def link Include Keyword
hi def link Macro Function
hi def link PreProc Keyword
hi def link WarningMsg Error
