.section .rodata
.global counter_elf
counter_elf:
.incbin "build/counter.elf"
.global counter_elf_end
counter_elf_end:
