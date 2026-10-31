#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
CC=${CC:-clang}
LD=${LD:-}
if [ -z "$LD" ]; then
 for candidate in /usr/local/opt/lld@21/bin/ld.lld /opt/homebrew/opt/lld/bin/ld.lld /usr/local/opt/lld/bin/ld.lld; do
  if [ -x "$candidate" ]; then LD=$candidate; break; fi
 done
fi
if [ -z "$LD" ]; then LD=ld.lld; fi
command -v "$CC" >/dev/null || { echo 'Install Xcode command line tools: xcode-select --install'; exit 1; }
command -v "$LD" >/dev/null || { echo 'Install linker: brew install lld'; exit 1; }
mkdir -p build
KFLAGS='--target=x86_64-unknown-none-elf -ffreestanding -fno-delete-null-pointer-checks -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -msse2 -mfpmath=sse -ffp-contract=off -O2 -Wall -Wextra -Werror -nostdlib -fno-builtin'
UFLAGS='--target=x86_64-unknown-none-elf -ffreestanding -fno-delete-null-pointer-checks -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -O2 -Wall -Wextra -Werror -nostdlib -fno-builtin'
UCFLAGS="$UFLAGS -mgeneral-regs-only -std=c11"
"$CC" $UFLAGS -c user/start.S -o build/user_start.o
"$CC" $UCFLAGS -c user/mem.c -o build/user_mem.o
"$CC" $UCFLAGS -c user/author.c -o build/author.o
"$CC" $UCFLAGS -c user/counter.c -o build/counter.o
"$LD" -m elf_x86_64 -z max-page-size=0x1000 -T user/user.ld -o build/author.elf build/user_start.o build/user_mem.o build/author.o
"$LD" -m elf_x86_64 -z max-page-size=0x1000 -T user/user.ld -o build/counter.elf build/user_start.o build/user_mem.o build/counter.o
"$CC" $UFLAGS -c user/guest_start.S -o build/guest_start.o
"$CC" $UCFLAGS -c user/guest.c -o build/guest.o
"$CC" $UFLAGS -c user/guest_blob.S -o build/guest_blob.o
"$LD" -m elf_x86_64 -z max-page-size=0x1000 -T user/guest.ld -o build/guest.elf build/guest_start.o build/user_mem.o build/guest.o build/guest_blob.o
python3 scripts/elfbin.py build/guest.elf build/guest.bin
"$CC" $UCFLAGS -c user/linux_init.c -o build/linux_init.o
"$LD" -m elf_x86_64 -z max-page-size=0x1000 -T user/linux_init.ld -o build/linux-init.elf build/linux_init.o
python3 scripts/mkcpio.py build/linux-init.elf build/guest-initrd.cpio
if [ -f build/linux/bzImage ]; then
  bz_bytes=$(wc -c < build/linux/bzImage | tr -d ' ')
else
  bz_bytes=0
fi
if [ "$bz_bytes" -gt 65536 ]; then
  cp -f build/linux/bzImage build/guest-bzImage
else
  printf '\0' > build/guest-bzImage
fi
"$CC" $KFLAGS -c kernel/payload.S -o build/payload.o
"$CC" $KFLAGS -c kernel/arch.S -o build/arch.o
"$CC" $KFLAGS -c kernel/blobs.S -o build/blobs.o
"$CC" $KFLAGS -std=c11 -c kernel/core.c -o build/core.o
"$CC" $KFLAGS -std=c11 -c kernel/asm.c -o build/asm.o
"$LD" -m elf_x86_64 -T kernel/kernel64.ld -o build/kernel64.elf build/payload.o build/core.o build/arch.o build/blobs.o build/asm.o
python3 scripts/elfbin.py build/kernel64.elf build/kernel64.bin
SFLAGS='--target=i686-none-elf -m32 -march=i686 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra -Werror'
"$CC" $SFLAGS -c kernel/stub.S -o build/stub.o
"$LD" -m elf_i386 -T kernel/stub.ld -o build/core.elf build/stub.o
python3 scripts/memory.py init build/memories.img
printf 'Built build/core.elf (Memories disk preserved)\n'
