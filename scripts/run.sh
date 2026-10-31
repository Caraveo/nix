#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
./scripts/build.sh
./scripts/qemu.sh --version >/dev/null
mode=${1:-}
set --
if [ -f models/smollm2-135m-core-q8.bin ]; then
 python3 scripts/model_bundle.py verify
 set -- -initrd models/smollm2-135m-core-q8.bin
else
 echo 'Local AI bundle not found; starting deterministic mode. See models/README.md.'
fi
if [ "$mode" = '--headless' ]; then
 exec ./scripts/qemu.sh -machine pc,accel=tcg -cpu phenom -m 512M -vga std -kernel build/core.elf "$@" -drive file=build/memories.img,format=raw,if=ide,index=0 -nic none -display none -serial stdio -monitor none
fi
if [ -f build/linux/userspace.cpio.gz ]; then
  python3 scripts/linux-seed.py build/linux/random-seeds.bin
  set -- "$@" -device loader,file=build/linux/userspace.cpio.gz,addr=0xb0000000,force-raw=on
  set -- "$@" -device loader,file=build/linux/random-seeds.bin,addr=0xbf000000,force-raw=on
  for slot in 0 1 2 3; do
    set -- "$@" -netdev "user,id=intent$slot" \
      -device "ne2k_pci,netdev=intent$slot,mac=52:54:00:4e:49:0$slot"
  done
fi
if [ ! -f build/install.img ]; then
  echo 'Creating the 10 GiB install disk.'
  mkfile -n 10g build/install.img 2>/dev/null || truncate -s 10G build/install.img
fi
: > build/serial.log
exec ./scripts/qemu.sh -name 'NIX / Core 0.3' -machine pc,accel=tcg -cpu phenom -m 8G -vga std -kernel build/core.elf "$@" -drive file=build/memories.img,format=raw,if=ide,index=0 -drive file=build/install.img,format=raw,if=ide,index=1 -nic none -serial file:build/serial.log
