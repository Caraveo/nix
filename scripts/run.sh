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
 exec ./scripts/qemu.sh -machine pc,accel=tcg -m 512M -vga std -kernel build/core.elf "$@" -drive file=build/memories.img,format=raw,if=ide,index=0 -nic none -display none -serial stdio -monitor none -no-reboot
fi
exec ./scripts/qemu.sh -name 'NIX / Core 0.2' -machine pc,accel=tcg -m 512M -vga std -kernel build/core.elf "$@" -drive file=build/memories.img,format=raw,if=ide,index=0 -nic none -serial file:build/serial.log -no-reboot
