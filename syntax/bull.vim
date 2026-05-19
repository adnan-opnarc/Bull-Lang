" Vim syntax file for Bull language
" Language: Bull
" File extension: .bl

if exists("b:current_syntax")
  finish
endif

syn keyword bullKeyword print input class struct int s8 s16 s32 s64 u8 u16 u32 u64
syn keyword bullKeyword glass bool char matrix array map call rc
syn keyword bullKeyword kernif kernelse else if return
syn keyword bullKeyword true false this var new len null
syn keyword bullKeyword while for read write open

syn match bullOperator "++\|--\|+=\|-=\|*=\|/=\|%=\|&=\||=\|^="
syn match bullOperator "==\|!=\|<\|>\|<=\|>=\|&&\|||\|<<\|>>\|->"
syn match bullOperator "[-+*/%&|^~!<>]"

syn region bullString start=+"+ skip=+\\\\\|\\"+ end=+"+

syn match bullComment "//.*$"
syn region bullComment start="/\*" end="\*/"

syn match bullNumber "\<[0-9][0-9_]*\>"
syn match bullNumber "\<0[xX][0-9a-fA-F][0-9a-fA-F_]*\>"

syn match bullPreProc "\[[^]]*\]"

highlight link bullKeyword Keyword
highlight link bullOperator Operator
highlight link bullString String
highlight link bullComment Comment
highlight link bullNumber Number
highlight link bullPreProc PreProc

let b:current_syntax = "bull"

augroup bullFileType
  autocmd!
  autocmd BufRead,BufNewFile *.bl setfiletype bull
augroup END
