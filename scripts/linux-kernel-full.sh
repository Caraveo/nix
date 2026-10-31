#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test -d build/linux/linux-7.3.1 || sh scripts/guest-stack.sh fetch
docker build --platform linux/amd64 -t nix-linux-builder:7.3.1-local \
    -f scripts/linux-build.Dockerfile scripts
docker run --rm --platform linux/amd64 \
    -v "$PWD/build/linux/linux-7.3.1:/src" -w /src nix-linux-builder:7.3.1-local sh -ceu '
for feature in BLOCK BLK_DEV BLK_DEV_NBD EXT4_FS OVERLAY_FS NET UNIX INET PACKET NETDEVICES ETHERNET PCI PCI_DIRECT \
    NET_VENDOR_NATSEMI NET_VENDOR_8390 NE2K_PCI \
    DEVTMPFS DEVTMPFS_MOUNT TTY UNIX98_PTYS PROC_FS SYSFS TMPFS SHMEM \
    EPOLL EVENTFD SIGNALFD TIMERFD FUTEX POSIX_TIMERS FILE_LOCKING MULTIUSER \
    ADVISE_SYSCALLS SYSVIPC PRINTK BLK_DEV_INITRD RD_GZIP BINFMT_ELF \
    BINFMT_SCRIPT INOTIFY_USER; do
    scripts/config --enable CONFIG_$feature
done
scripts/config --disable CONFIG_PREEMPT_LAZY
scripts/config --enable CONFIG_PREEMPT
make ARCH=x86_64 olddefconfig
make -j12 ARCH=x86_64 bzImage
'
cp build/linux/linux-7.3.1/arch/x86/boot/bzImage build/linux/bzImage
touch build/linux/full-kernel.ready
echo 'Built Linux 7.3.1 with networking and general userspace support.'
