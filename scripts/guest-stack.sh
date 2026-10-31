#!/bin/sh
# Linux 7.3.1 guest stacks. NIX itself is not Linux and stays the host.
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
SRC_URL=https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.3.1.tar.xz
WORKDIR=$ROOT/build/linux
TARBALL=$WORKDIR/linux-7.3.1.tar.xz
TREE=$WORKDIR/linux-7.3.1
BZ=$WORKDIR/bzImage
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

need_docker() {
  if ! docker info >/dev/null 2>&1; then
    echo 'Docker is not running. Start Docker Desktop, then retry.' >&2
    exit 1
  fi
}

fetch_kernel() {
  mkdir -p "$WORKDIR"
  if [ ! -f "$TARBALL" ]; then
    echo 'Downloading Linux 7.3.1 from kernel.org'
    curl -fL --retry 3 -o "$TARBALL.part" "$SRC_URL"
    mv "$TARBALL.part" "$TARBALL"
  fi
  if [ ! -d "$TREE" ]; then
    echo 'Extracting Linux 7.3.1'
    tar -xJf "$TARBALL" -C "$WORKDIR"
  fi
}

build_kernel() {
  if [ -f "$BZ" ]; then
    echo "Linux 7.3.1 bzImage already built: $BZ"
    return
  fi
  need_docker
  fetch_kernel
  echo 'Building Linux 7.3.1 (guest config, serial + initramfs). NIX is not Linux.'
  docker run --rm -v "$TREE:/src" -w /src debian:stable bash -ceu '
    apt-get update
    apt-get install -y --no-install-recommends build-essential bc bison flex libelf-dev libssl-dev cpio
    make ARCH=x86_64 tinyconfig
    scripts/config --enable CONFIG_64BIT
    scripts/config --enable CONFIG_PRINTK
    scripts/config --enable CONFIG_PRINTK_TIME
    scripts/config --enable CONFIG_EARLY_PRINTK
    scripts/config --enable CONFIG_TTY
    scripts/config --enable CONFIG_SERIAL_8250
    scripts/config --enable CONFIG_SERIAL_8250_CONSOLE
    scripts/config --enable CONFIG_BLK_DEV_INITRD
    scripts/config --enable CONFIG_RD_GZIP
    scripts/config --enable CONFIG_BINFMT_ELF
    scripts/config --enable CONFIG_BINFMT_SCRIPT
    scripts/config --enable CONFIG_PROC_FS
    scripts/config --enable CONFIG_SYSFS
    scripts/config --enable CONFIG_DEVTMPFS
    scripts/config --enable CONFIG_DEVTMPFS_MOUNT
    scripts/config --enable CONFIG_TMPFS
    scripts/config --enable CONFIG_EXT4_FS
    scripts/config --enable CONFIG_UNIX
    scripts/config --enable CONFIG_INET
    scripts/config --enable CONFIG_MULTIUSER
    scripts/config --set-str CONFIG_CMDLINE "console=ttyS0"
    scripts/config --enable CONFIG_CMDLINE_BOOL
    make ARCH=x86_64 olddefconfig
    make -j"'"$JOBS"'" ARCH=x86_64 bzImage
  '
  cp "$TREE/arch/x86/boot/bzImage" "$BZ"
  echo "Built $BZ"
}

rootfs() {
  name=$1
  image=$2
  out=$WORKDIR/$name.cpio
  if [ -f "$out" ]; then
    echo "Rootfs exists: $out"
    return
  fi
  need_docker
  echo "Exporting $image userspace for $name"
  cid=$(docker create "$image")
  docker export "$cid" > "$WORKDIR/$name.tar"
  docker rm "$cid" >/dev/null
  docker run --rm -v "$WORKDIR:/out" debian:stable bash -ceu "
    apt-get update >/dev/null
    apt-get install -y --no-install-recommends cpio >/dev/null
    mkdir -p /tmp/root
    tar -xf /out/$name.tar -C /tmp/root
    cd /tmp/root
    find . -print0 | cpio --null -o -H newc > /out/$name.cpio
  "
  rm -f "$WORKDIR/$name.tar"
  echo "Wrote $out"
}

boot_one() {
  name=$1
  case "$name" in
    debian) image=debian:stable ;;
    ubuntu) image=ubuntu:latest ;;
    centos) image=quay.io/centos/centos:stream9 ;;
    *) echo "No Linux 7.3.1 stack for $name. BSD is not this kernel." >&2; exit 1 ;;
  esac
  build_kernel
  rootfs "$name" "$image"
  echo "Booting $name on Linux 7.3.1. NIX is not Linux."
  exec ./scripts/qemu.sh -name "$name on Linux 7.3.1" \
    -machine pc,accel=tcg -cpu phenom -m 768 -nic none -nographic \
    -kernel "$BZ" -initrd "$WORKDIR/$name.cpio" \
    -append "console=ttyS0 rdinit=/bin/bash"
}

cmd=${1:-}
case "$cmd" in
  fetch) fetch_kernel ;;
  build) build_kernel ;;
  boot) boot_one "${2:-debian}" ;;
  *) echo 'usage: guest-stack.sh fetch|build|boot debian|ubuntu|centos' >&2; exit 2 ;;
esac
