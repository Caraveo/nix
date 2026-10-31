#!/bin/sh
# Linux PID 1. The host remains NIX; this filesystem belongs to one guest.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
/bin/busybox mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /proc /sys /run /tmp /root
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /run
# Mount the named instance as a complete writable root over the base template.
case " $(cat /proc/cmdline) " in
 *" nix.persist=1 "*)
    /sbin/nix-block >/run/nix-block.log 2>&1 &
    for attempt in $(seq 1 100); do
        [ "$(cat /sys/block/nbd0/size 2>/dev/null)" = 41943040 ] && break
        sleep .1
    done
    case " $(cat /proc/cmdline) " in
      *" nix.fresh=1 "*)
        mkfs.ext4 -q -F -m 0 -E nodiscard,lazy_itable_init=1,lazy_journal_init=1 /dev/nbd0 || exec /bin/sh;;
    esac
    mkdir -p /run/instance /run/nix-root
    mount -t ext4 -o sync,noatime /dev/nbd0 /run/instance || exec /bin/sh
    mkdir -p /run/instance/upper /run/instance/work
    mount -t overlay overlay -o lowerdir=/,upperdir=/run/instance/upper,workdir=/run/instance/work /run/nix-root || exec /bin/sh
    mkdir -p /run/nix-root/dev /run/nix-root/proc /run/nix-root/sys /run/nix-root/run
    mount --rbind /dev /run/nix-root/dev
    mount -t proc proc /run/nix-root/proc
    mount -t sysfs sysfs /run/nix-root/sys
    mount --bind /run /run/nix-root/run
    /sbin/nix-ready || exec /bin/sh
    exec chroot /run/nix-root /sbin/nix-start
    ;;
esac
exec /sbin/nix-start
