#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 scripts/model_bundle.py verify
python3 scripts/memory.py init build/memories.img
./scripts/qemu.sh --version >/dev/null
exec ./scripts/qemu.sh -name 'NIX / Core 0.2 - Local AI' -machine pc,accel=tcg -m 512M -vga std -kernel build/core.elf -initrd models/smollm2-135m-core-q8.bin -drive file=build/memories.img,format=raw,if=ide,index=0 -nic none -serial file:build/serial.log
