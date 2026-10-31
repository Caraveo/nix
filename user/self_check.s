; Corpus for the resident assembler. Long encodings, org 0x8000.
org 0x8000

entry:
  nop
  mov rax, 0x1122334455667788
  mov eax, 0x12345678
  mov al, 0x7f
  mov rcx, rax
  mov eax, ecx
  mov al, bl
  mov rax, [rbx]
  mov [rbx+0x20], rax
  mov eax, [rbx+rcx*4+0x10]
  mov rax, [message]
  mov eax, [0x1000]
  lea rdi, [rsi+0x30]
  lea rax, [message]
  add rax, rbx
  add eax, 1
  sub rcx, 8
  xor eax, eax
  or rax, rdx
  and rax, 0xff
  cmp rax, 0
  test eax, eax
  inc eax
  dec rbx
  neg rax
  not ecx
  shl rax, 3
  shr eax, 1
  sar rax, cl
  rol eax, 1
  push rax
  pop rbx
  push 0x2a
  imul rax, rbx
  imul rax, rbx, 5
  movzx eax, bl
  movsx rax, eax
  xchg rax, rbx
  jmp entry
  call entry
  jz entry
  jne entry
  jb entry
  jae entry
  jl entry
  jg entry
  rep movsb
  rep stosq
  movsb
  stosb
  lodsb
  cld
  clc
  pushfq
  popfq
  cdqe
  cqo
  cpuid
  syscall
  vmmcall
  hlt
  ud2
  ret
  leave
message:
  db "nix", 0
  dw 0x1234
  dd 0x89abcdef
  dq 0x100
len equ 4
  mov eax, len
message_end:
  align 16
