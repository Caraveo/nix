#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
./scripts/build.sh
./scripts/qemu.sh --version >/dev/null
if [ "${1:-}" = '--headless' ]; then
 exec ./scripts/qemu.sh -machine pc,accel=tcg -m 64M -vga std -kernel build/core.elf -drive file=build/memories.img,format=raw,if=ide,index=0 -nic none -display none -serial stdio -monitor none -no-reboot
fi
exec ./scripts/qemu.sh -name 'NIX / Core 0.1' -machine pc,accel=tcg -m 64M -vga std -kernel build/core.elf -drive file=build/memories.img,format=raw,if=ide,index=0 -nic none -serial file:build/serial.log -no-reboot
