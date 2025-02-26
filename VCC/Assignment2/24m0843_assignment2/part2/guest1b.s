# User Morpheus VM

.globl _start
    .code16
_start:
    xorw %ax, %ax
    
loop1:
    in $0x11, %ax
    out %ax, $0x12
    jmp loop1
