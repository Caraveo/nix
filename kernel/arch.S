.section .bss
.align 16
.skip 8192
.global fault_stack_top
fault_stack_top:

.align 8
saved_user_rsp: .quad 0
saved_user_rip: .quad 0
saved_user_rflags: .quad 0
saved_user_rdi: .quad 0
saved_user_rsi: .quad 0
saved_user_rdx: .quad 0
saved_user_r8: .quad 0
saved_user_r9: .quad 0
saved_user_r10: .quad 0
resume_rsp: .quad 0
resume_rip: .quad 0

.section .text
.global syscall_entry
/* Preserve every GPR except rax, rcx, and r11. Clang keeps the line
   pointer in rdx and r8 across syscall, and the user wrapper only
   lists the two registers the instruction itself overwrites. */
syscall_entry:
  mov %rsp, saved_user_rsp(%rip)
  mov %rcx, saved_user_rip(%rip)
  mov %r11, saved_user_rflags(%rip)
  mov %rdi, saved_user_rdi(%rip)
  mov %rsi, saved_user_rsi(%rip)
  mov %rdx, saved_user_rdx(%rip)
  mov %r8, saved_user_r8(%rip)
  mov %r9, saved_user_r9(%rip)
  mov %r10, saved_user_r10(%rip)
  mov $aux_stack_top, %rsp
  mov %rax, %rdi
  mov saved_user_rdi(%rip), %rsi
  mov saved_user_rsi(%rip), %rdx
  cld
  call syscall_dispatch
  mov saved_user_rdi(%rip), %rdi
  mov saved_user_rsi(%rip), %rsi
  mov saved_user_rdx(%rip), %rdx
  mov saved_user_r8(%rip), %r8
  mov saved_user_r9(%rip), %r9
  mov saved_user_r10(%rip), %r10
  mov saved_user_rip(%rip), %rcx
  mov saved_user_rflags(%rip), %r11
  or $2, %r11
  mov saved_user_rsp(%rip), %rsp
  sysretq

.global user_launch
user_launch:
  push %rbx
  push %rbp
  push %r12
  push %r13
  push %r14
  push %r15
  mov %rsp, resume_rsp(%rip)
  lea resume_here(%rip), %rax
  mov %rax, resume_rip(%rip)
  pushq $0x1b
  push %rsi
  pushq $2
  pushq $0x23
  push %rdi
  iretq
resume_here:
  pop %r15
  pop %r14
  pop %r13
  pop %r12
  pop %rbp
  pop %rbx
  ret

.global user_return
user_return:
  mov $0x10, %ax
  mov %ax, %ss
  mov resume_rsp(%rip), %rsp
  mov user_result(%rip), %eax
  jmp resume_here

.global exc_common
exc_common:
  testb $3, 24(%rsp)
  jz kernel_exc
  movl $1, user_fault(%rip)
  mov $0x10, %ax
  mov %ax, %ss
  jmp user_return
kernel_exc:
  mov 8(%rsp), %rdi
  mov 16(%rsp), %rsi
  mov %cr2, %rdx
  cld
  call kernel_fault
1:
  hlt
  jmp 1b

.macro EXC vec, haserr
.globl exc_\vec
exc_\vec:
  .ifeq \haserr
  pushq $0
  .endif
  pushq $\vec
  jmp exc_common
.endm

EXC 0, 0
EXC 1, 0
EXC 2, 0
EXC 3, 0
EXC 4, 0
EXC 5, 0
EXC 6, 0
EXC 7, 0
EXC 8, 1
EXC 9, 0
EXC 10, 1
EXC 11, 1
EXC 12, 1
EXC 13, 1
EXC 14, 1
EXC 15, 0
EXC 16, 0
EXC 17, 1
EXC 18, 0
EXC 19, 0
EXC 20, 0
EXC 21, 1
EXC 22, 0
EXC 23, 0
EXC 24, 0
EXC 25, 0
EXC 26, 0
EXC 27, 0
EXC 28, 0
EXC 29, 0
EXC 30, 0
EXC 31, 0

.global irq_entry
irq_entry:
  pushq $0
  pushq $32
  jmp exc_common

.section .rodata
.global exc_table
exc_table:
  .quad exc_0
  .quad exc_1
  .quad exc_2
  .quad exc_3
  .quad exc_4
  .quad exc_5
  .quad exc_6
  .quad exc_7
  .quad exc_8
  .quad exc_9
  .quad exc_10
  .quad exc_11
  .quad exc_12
  .quad exc_13
  .quad exc_14
  .quad exc_15
  .quad exc_16
  .quad exc_17
  .quad exc_18
  .quad exc_19
  .quad exc_20
  .quad exc_21
  .quad exc_22
  .quad exc_23
  .quad exc_24
  .quad exc_25
  .quad exc_26
  .quad exc_27
  .quad exc_28
  .quad exc_29
  .quad exc_30
  .quad exc_31

.section .text
.global svm_vmrun
svm_vmrun:
  mov %rdi, %rax
  push %rbx
  push %rbp
  push %r12
  push %r13
  push %r14
  push %r15
  sub $520, %rsp
  fxsave (%rsp)
  .byte 0x0f, 0x01, 0xd8
  fxrstor (%rsp)
  add $520, %rsp
  pop %r15
  pop %r14
  pop %r13
  pop %r12
  pop %rbp
  pop %rbx
  ret

# rdi = vmcb, rsi = 14 guest qwords: rcx rdx rbx rbp rsi rdi r8..r15.
# Eight pushes keep fxsave 16-byte aligned. Guest rax stays in the VMCB.
.global linux_vmrun
linux_vmrun:
  push %rbx
  push %rbp
  push %r12
  push %r13
  push %r14
  push %r15
  push %rsi
  push %rsi
  sub $520, %rsp
  fxsave (%rsp)
  mov %rdi, %rax
  mov 528(%rsp), %rsi
  mov 0(%rsi), %rcx
  mov 8(%rsi), %rdx
  mov 16(%rsi), %rbx
  mov 24(%rsi), %rbp
  mov 48(%rsi), %r8
  mov 56(%rsi), %r9
  mov 64(%rsi), %r10
  mov 72(%rsi), %r11
  mov 80(%rsi), %r12
  mov 88(%rsi), %r13
  mov 96(%rsi), %r14
  mov 104(%rsi), %r15
  mov 40(%rsi), %rdi
  mov 32(%rsi), %rsi
  .byte 0x0f, 0x01, 0xd8
  mov 528(%rsp), %rax
  mov %rcx, 0(%rax)
  mov %rdx, 8(%rax)
  mov %rbx, 16(%rax)
  mov %rbp, 24(%rax)
  mov %rsi, 32(%rax)
  mov %rdi, 40(%rax)
  mov %r8, 48(%rax)
  mov %r9, 56(%rax)
  mov %r10, 64(%rax)
  mov %r11, 72(%rax)
  mov %r12, 80(%rax)
  mov %r13, 88(%rax)
  mov %r14, 96(%rax)
  mov %r15, 104(%rax)
  fxrstor (%rsp)
  add $520, %rsp
  pop %rsi
  pop %rsi
  pop %r15
  pop %r14
  pop %r13
  pop %r12
  pop %rbp
  pop %rbx
  ret
