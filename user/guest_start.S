.global _start
_start:
  mov $0x7000, %rsp
  sub $8, %rsp
  xor %ebp, %ebp
  call main
  mov $1, %eax
  .byte 0x0f, 0x01, 0xd9
  jmp _start
