.section .rodata
.global author_elf, author_elf_end, guest_bin, guest_bin_end
.global intent_src, intent_src_end, self_src, self_src_end
.global linux_image, linux_image_end, linux_initrd, linux_initrd_end
author_elf:
.incbin "build/author.elf"
author_elf_end:
guest_bin:
.incbin "build/guest.bin"
guest_bin_end:
intent_src:
.incbin "user/intent_kernel.s"
intent_src_end:
self_src:
.incbin "user/self_check.s"
self_src_end:
linux_image:
.incbin "build/guest-bzImage"
linux_image_end:
linux_initrd:
.incbin "build/guest-initrd.cpio"
linux_initrd_end:
