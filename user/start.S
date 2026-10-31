.section .data
.global pod_anchor
pod_anchor:
.byte 0

.section .text
.global _start
_start:
  mov $0x2200000, %rsp
  sub $8, %rsp
  xor %ebp, %ebp
  call main
  mov $3, %eax
  xor %edi, %edi
  syscall
  jmp _start
