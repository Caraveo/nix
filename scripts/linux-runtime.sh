#!/bin/sh
# Prepare the Linux execution template used by NIX intents.
set -eu
cd "$(dirname "$0")/.."
if [ ! -f build/linux/userspace.cpio.gz ]; then
  sh scripts/linux-userspace.sh
fi
python3 scripts/linux-userspace-init.py
sh scripts/linux-kernel-full.sh
./scripts/build.sh
if ! ./scripts/qemu.sh -netdev help | grep -q '^user$'; then
  echo 'QEMU needs the user network backend (libslirp) to connect Linux intents.' >&2
  exit 1
fi
echo 'Linux runtime ready. Launch ./scripts/run.sh, then ask kernel and confirm.'
