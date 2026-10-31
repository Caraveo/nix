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
 # Floating-point inference uses SSE2; preserve Multiboot arguments in eax/ebx.
 mov %cr0, %ecx
 and $0xfffffff3, %ecx
 or $2, %ecx
 mov %ecx, %cr0
 mov %cr4, %ecx
 or $0x600, %ecx
 mov %ecx, %cr4
 fninit
 # Clear all runtime state on every boot, including firmware-driven resets.
 mov %eax, %esi
 mov $__bss_start, %edi
 mov $__bss_end, %ecx
 sub %edi, %ecx
 xor %eax, %eax
 rep stosb
 mov %esi, %eax
 mov $stack_top, %esp
 xor %ebp, %ebp
 sub $8, %esp
 push %ebx
 push %eax
 call kernel_main
1: hlt
 jmp 1b
