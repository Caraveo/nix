OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS { load PT_LOAD FLAGS(5); }
SECTIONS {
  . = 0x400000;
  .text : {
    *(.text .text.*)
    *(.rodata .rodata.*)
    *(.data .data.*)
    *(.bss .bss.*)
    *(COMMON)
  } :load
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
