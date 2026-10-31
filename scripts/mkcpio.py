#!/usr/bin/env python3
"""Pack a newc initramfs. The guest init is Linux userspace, not a NIX pod."""
import sys
from pathlib import Path


def entry(ino, mode, name, data=b"", rmajor=0, rminor=0, nlink=1):
    raw = name.encode("ascii") + b"\0"
    nums = [ino, mode, 0, 0, nlink, 0, len(data), 0, 0, rmajor, rminor, len(raw), 0]
    blob = b"070701" + b"".join(f"{n:08X}".encode() for n in nums)
    if len(blob) != 110:
        raise SystemExit("cpio header size")
    blob += raw
    blob += b"\0" * ((4 - (len(blob) % 4)) % 4)
    blob += data
    blob += b"\0" * ((4 - (len(data) % 4)) % 4)
    return blob


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: mkcpio.py init.elf out.cpio")
    init = Path(sys.argv[1]).read_bytes()
    if init[:4] != b"\x7fELF":
        raise SystemExit("init is not an ELF")
    blob = b"".join([
        entry(1, 0o040755, "dev", nlink=2),
        entry(2, 0o020666, "dev/console", rmajor=5, rminor=1),
        entry(3, 0o040755, "proc", nlink=2),
        entry(4, 0o0100755, "init", init),
        entry(0, 0, "TRAILER!!!"),
    ])
    Path(sys.argv[2]).write_bytes(blob)


if __name__ == "__main__":
    main()
