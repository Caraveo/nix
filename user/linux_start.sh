#!/bin/sh
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
chmod 1777 /tmp
rm -f /.dockerenv /nix-init
hostname nix-linux
ip link set lo up
if ip link show eth0 >/dev/null 2>&1; then
    ip addr add 10.0.2.15/24 dev eth0
    ip link set eth0 up
    ip route add default via 10.0.2.2
    echo 'nameserver 10.0.2.3' > /etc/resolv.conf
    echo 'Guest network: ready (private NAT).'
else
    echo 'Guest network: no adapter attached.'
fi
echo 'Linux guest online. NIX is the host.'
echo 'Full Linux userspace. Type help for shell builtins; ls /usr/bin for programs.'
echo 'curl, wget, ping, python3, git, apt, and standard Linux tools are available.'
echo 'Type nix and Enter to close this tab. Named instances retain their complete filesystem.'
export HOME=/root USER=dios LOGNAME=dios TERM=dumb
export PS1='linux > '
cd /root
while :; do
    /bin/busybox setsid -c /bin/bash --noprofile --norc -i </dev/ttyS0 >/dev/ttyS0 2>&1
done
