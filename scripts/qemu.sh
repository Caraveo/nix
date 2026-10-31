#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
if [ -n "${QEMU:-}" ]; then exec "$QEMU" "$@"; fi
if command -v qemu-system-x86_64 >/dev/null; then exec qemu-system-x86_64 "$@"; fi
# A minimal local QEMU build is also supported on Intel macOS.
for candidate in "$root"/build/tools/qemu-*/build/qemu-system-x86_64; do
 if [ -x "$candidate" ]; then exec "$candidate" "$@"; fi
done
printf 'QEMU not found. Install with brew install qemu, or set QEMU to its executable path.\n' >&2
exit 1
