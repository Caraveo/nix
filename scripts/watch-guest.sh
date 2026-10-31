#!/bin/sh
# Watch a NIX serial log and boot a requested Linux 7.3.1 stack.
# NIX is the host. The guest is not NIX.
set -eu
cd "$(dirname "$0")/.."
log=${1:-build/serial.log}
parent=${2:-1}
mkdir -p build/linux
while kill -0 "$parent" 2>/dev/null; do
  if [ -f "$log" ]; then
    if grep -q 'NIX_BUILD_KERNEL linux-7.3.1' "$log" && [ ! -f build/linux/build.started ]; then
      touch build/linux/build.started
      ./scripts/guest-stack.sh build > build/linux/build.log 2>&1 || echo 'Linux 7.3.1 build failed' >> build/linux/build.log
    fi
    for name in debian ubuntu centos; do
      if grep -q "NIX_BOOT_GUEST $name" "$log" && [ ! -f "build/linux/boot.$name" ]; then
        touch "build/linux/boot.$name"
        osascript -e "tell application \"Terminal\" to do script \"cd '$PWD' && ./scripts/guest-stack.sh boot $name\"" >/dev/null 2>&1 || \
          ./scripts/guest-stack.sh boot "$name" > "build/linux/$name.log" 2>&1 &
      fi
    done
  fi
  sleep 2
done
