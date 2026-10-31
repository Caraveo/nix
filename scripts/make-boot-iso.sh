#!/bin/sh
# Bootable x86-64 ISO of NIX. The Mac is the build machine.
# The disc is El Torito (BIOS) and UEFI. It is not a Mac application.
set -eu
cd "$(dirname "$0")/.."
VERSION=$(tr -d '[:space:]' < VERSION)
STAGE=build/pc-iso
OUT="build/NIX-${VERSION}-pc.iso"
test -f build/core.elf || {
  echo 'Build the kernel first: ./scripts/build.sh'
  exit 1
}
command -v docker >/dev/null || {
  echo 'Docker is required to pack the bootable ISO.'
  exit 1
}
rm -rf "$STAGE"
mkdir -p "$STAGE/boot/grub"
cp -f build/core.elf "$STAGE/boot/core.elf"
cat > "$STAGE/boot/grub/grub.cfg" << 'EOF'
set timeout=0
set default=0
menuentry "NIX" {
  multiboot /boot/core.elf
  boot
}
EOF
docker run --rm -v "$PWD:/work" -w /work debian:stable bash -lc '
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends grub-pc-bin grub-efi-amd64-bin xorriso mtools
grub-mkrescue -volid NIX-0.3 -o /work/build/NIX-pc-tmp.iso /work/build/pc-iso
'
mv -f build/NIX-pc-tmp.iso "$OUT"
echo "Bootable ISO: $OUT"
