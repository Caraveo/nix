ENTRY(_start)
SECTIONS {
  . = 1M;
  .multiboot : { KEEP(*(.multiboot)) }
  .text : { *(.text*) *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(COMMON) *(.bss*) }
  . = 0x180000;
  .pagetables (NOLOAD) : {
    pml4 = .;
    . += 4096;
    pdpt = .;
    . += 4096;
    pd = .;
    . += 16384;
    userpt = .;
    . += 4096;
  }
  . = 2M;
  .kernel : { *(.kernel) }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
