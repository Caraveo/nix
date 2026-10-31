ENTRY(kernel_main)
SECTIONS {
  . = 2M;
  .payload : { KEEP(*(.payload)) }
  .text ALIGN(16) : { *(.text*) }
  .rodata ALIGN(16) : { *(.rodata*) }
  .data ALIGN(16) : { *(.data*) }
  .bss ALIGN(4096) : { __bss_start = .; *(COMMON) *(.bss*) __bss_end = .; }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
