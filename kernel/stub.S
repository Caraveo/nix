/* Multiboot v1 entry. QEMU rejects an ELF64 kernel, so this stub stays ELF32,
   enables long mode, and calls the ELF64 image loaded at 2 MiB. */
.section .multiboot, "a"
.align 4
.long 0x1badb002
.long 0x00000003
.long -(0x1badb002 + 0x00000003)

.section .data
magic_store: .long 0
info_store: .long 0
gdt:
  .quad 0
  .quad 0x00af9a000000ffff
  .quad 0x00cf92000000ffff
gdt_ptr:
  .word 23
  .long gdt

.section .bss
.align 16
.skip 4096
stub_stack:

.section .text
.code32
.global _start
_start:
  cli
  cld
  mov %eax, 0x17ff00
  mov %ebx, 0x17ff04
  mov $stub_stack, %esp
  lgdt gdt_ptr
  mov $0x10, %eax
  mov %eax, %ds
  mov %eax, %es
  mov %eax, %ss
  /* QEMU places the Multiboot module just after this image, inside the
     payload BSS cleared below. Copy one module to 256 MiB first.
     0x17ff10 is its physical address and 0x17ff14 is its length.
     No module, or a guest too small to hold it, leaves both zero. */
  xor %eax, %eax
  mov %eax, 0x17ff10
  mov %eax, 0x17ff14
  mov 0x17ff04, %ebx
  test %ebx, %ebx
  jz no_module
  mov (%ebx), %eax
  and $9, %eax
  cmp $9, %eax
  jne no_module
  cmpl $1, 20(%ebx)
  jne no_module
  mov 24(%ebx), %edx
  test %edx, %edx
  jz no_module
  mov (%edx), %esi
  mov 4(%edx), %ecx
  sub %esi, %ecx
  jbe no_module
  cmp $0x10000000, %ecx
  ja no_module
  mov 8(%ebx), %eax
  cmp $0x400000, %eax
  jae module_fits
  shl $10, %eax
  sub $0x0FF00000, %eax
  jb no_module
  cmp %ecx, %eax
  jb no_module
module_fits:
  mov $0x10000000, %edi
  mov %edi, %eax
  add %ecx, %eax
  jc no_module
  cmp %eax, %esi
  jae module_place
  mov %esi, %eax
  add %ecx, %eax
  jc no_module
  cmp %edi, %eax
  jbe module_place
  jmp no_module
module_place:
  mov %edi, 0x17ff10
  mov %ecx, 0x17ff14
  rep movsb
no_module:
  /* Zero the long-mode image BSS. The header at 2 MiB is loaded file data. */
  mov 0x200008, %edi
  mov 0x200010, %ecx
  sub %edi, %ecx
  jbe zero_skip
  xor %eax, %eax
  rep stosb
zero_skip:
  /* Identity-map 64 GiB. Only the PC PCI hole at 3..4 GiB is uncached. */
  mov $pml4, %edi
  mov $0x42000, %ecx
  xor %eax, %eax
  rep stosb
  mov $pml4, %eax
  mov $pdpt, %ebx
  or $7, %ebx
  mov %ebx, (%eax)
  mov $pd, %eax
  mov $pdpt, %edi
  mov $64, %ecx
fill_pdpt:
  mov %eax, %ebx
  or $7, %ebx
  mov %ebx, (%edi)
  add $8, %edi
  add $4096, %eax
  loop fill_pdpt
  mov $pd, %edi
  xor %ebx, %ebx
  xor %edx, %edx
  mov $32768, %ecx
fill_pd:
  mov %ebx, %eax
  or $0x83, %eax
  test %edx, %edx
  jnz cacheable
  cmp $0xc0000000, %ebx
  jb cacheable
  or $0x10, %eax
cacheable:
  mov %eax, (%edi)
  mov %edx, 4(%edi)
  add $8, %edi
  add $0x200000, %ebx
  adc $0, %edx
  loop fill_pd
  mov $pml4, %eax
  mov %eax, %cr3
  mov %cr4, %eax
  or $0x20, %eax
  mov %eax, %cr4
  mov $0xc0000080, %ecx
  rdmsr
  or $0x901, %eax
  wrmsr
  mov %cr0, %eax
  or $0x80000001, %eax
  mov %eax, %cr0
  ljmp $0x08, $long_mode

.code64
long_mode:
  xor %ax, %ax
  mov %ax, %ds
  mov %ax, %es
  mov %ax, %fs
  mov %ax, %gs
  mov $0x10, %ax
  mov %ax, %ss
  mov %cr0, %rax
  and $0xfffffffffffffffb, %rax
  or $2, %rax
  mov %rax, %cr0
  mov %cr4, %rax
  or $0x600, %rax
  mov %rax, %cr4
  fninit
  /* Absolute immediates only: this ELF32 link cannot relocate 64-bit symbols. */
  movabs $0x200018, %rax
  mov (%rax), %rsp
  xor %ebp, %ebp
  movabs $0x17ff00, %rax
  mov (%rax), %edi
  mov 4(%rax), %esi
  movabs $0x200000, %rax
  mov (%rax), %rax
  call *%rax
1:
  hlt
  jmp 1b

.section .kernel, "a"
.incbin "build/kernel64.bin"
