#!/usr/bin/env python3
"""Refresh PID 1 without downloading packages again (concatenated initramfs)."""
import gzip
from pathlib import Path
from mkcpio import entry

root = Path(__file__).resolve().parents[1]
base = root / 'build/linux/userspace-base.cpio.gz'
out = root / 'build/linux/userspace.cpio.gz'
if not base.exists():
    out.replace(base)
overlay = entry(1, 0o040755, 'dev', nlink=2)
overlay += entry(2, 0o020600, 'dev/console', rmajor=5, rminor=1)
overlay += entry(3, 0o100755, 'init', (root / 'user/linux_full_init.sh').read_bytes())
overlay += entry(4, 0o100755, 'sbin/nix-start', (root / 'user/linux_start.sh').read_bytes())
bridge = root / 'build/linux/nix-block'
if bridge.exists():
    overlay += entry(5, 0o100755, 'sbin/nix-block', bridge.read_bytes())
overlay += entry(0, 0, 'TRAILER!!!')
temp = out.with_suffix('.gz.part')
with temp.open('wb') as stream:
    with base.open('rb') as source:
        while chunk := source.read(1024 * 1024):
            stream.write(chunk)
    stream.write(gzip.compress(overlay, mtime=0))
temp.replace(out)
