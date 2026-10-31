ENTRY(_start)
SECTIONS {
 . = 1M;
 .multiboot : { KEEP(*(.multiboot)) }
 .text ALIGN(4096) : { *(.text*) }
 .rodata ALIGN(4096) : { *(.rodata*) }
 .data ALIGN(4096) : { *(.data*) }
 .bss ALIGN(4096) : { __bss_start = .; *(COMMON) *(.bss*) __bss_end = .; }
 /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
