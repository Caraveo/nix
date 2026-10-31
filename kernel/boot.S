.section .multiboot,"a"
.align 4
.long 0x1badb002
.long 0x00000003
.long -(0x1badb002 + 0x00000003)
.section .bss
.align 16
stack_bottom:
.skip 32768
stack_top:
.section .text
.global _start
.type _start,@function
_start:
 cli
 cld
 mov $stack_top, %esp
 xor %ebp, %ebp
 sub $8, %esp
 push %ebx
 push %eax
 call kernel_main
1: hlt
 jmp 1b
