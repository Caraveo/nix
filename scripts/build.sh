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
FLAGS='--target=i686-none-elf -m32 -march=i686 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-sse -mno-mmx -msoft-float -O2 -Wall -Wextra -Werror'
"$CC" $FLAGS -c kernel/boot.S -o build/boot.o
"$CC" $FLAGS -std=c11 -c kernel/core.c -o build/core.o
"$LD" -m elf_i386 -T kernel/linker.ld -o build/core.elf build/boot.o build/core.o
python3 scripts/memory.py init build/memories.img
printf 'Built build/core.elf (Memories disk preserved)\n'
