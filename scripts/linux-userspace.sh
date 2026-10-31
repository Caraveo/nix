#!/bin/sh
# Build a reusable Debian userspace template. No host filesystem is exported.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/linux
docker run --rm --platform linux/amd64 \
    -v "$PWD/build/linux:/out" -v "$PWD/user/linux_full_init.sh:/nix-init:ro" \
    debian:stable sh -ceu '
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends busybox-static bash coreutils \
    curl wget iputils-ping iproute2 ca-certificates python3 git jq \
    nano less openssh-client netcat-openbsd dnsutils findutils file \
    procps psmisc util-linux make tcc libc6-dev strace cpio gzip xz-utils
ln -sf /usr/bin/tcc /usr/local/bin/cc
cp /nix-init /init
chmod 755 /init
printf "dios:x:0:0:dios:/root:/bin/bash\n" > /etc/passwd
printf "dios:x:0:\n" > /etc/group
printf "nameserver 10.0.2.3\n" > /etc/resolv.conf
dpkg-query -W > /out/userspace-packages.txt
apt-get clean
rm -rf /var/lib/apt/lists/* /usr/share/doc/* /usr/share/man/* /usr/share/locale/*
cd /
find . -xdev \( -path ./proc -o -path ./sys -o -path ./dev -o -path ./out \) -prune -o -print0 |
    cpio --null -o -H newc --quiet | gzip -1 > /out/userspace.cpio.gz.part
mv /out/userspace.cpio.gz.part /out/userspace-base.cpio.gz
'
python3 scripts/linux-userspace-init.py
python3 - <<'PY'
from pathlib import Path
import hashlib
p = Path('build/linux/userspace.cpio.gz')
assert p.stat().st_size < 128 * 1024 * 1024, 'Userspace template exceeds its reserved window'
print(f'Linux userspace template: {p.stat().st_size} bytes, SHA256 {hashlib.sha256(p.read_bytes()).hexdigest()}')
PY
