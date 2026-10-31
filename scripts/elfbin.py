#!/usr/bin/env python3
"""Flatten an ELF64 image to the raw bytes QEMU's Multiboot loader can embed."""
import struct
import sys

def main():
    src, dst = sys.argv[1:]
    data = open(src, 'rb').read()
    if data[:4] != b'\x7fELF' or data[4] != 2:
        raise SystemExit('expected an ELF64 file')
    endian = '<'
    e_phoff, = struct.unpack_from(endian + 'Q', data, 32)
    e_phentsize, e_phnum = struct.unpack_from(endian + 'HH', data, 54)
    low, high = None, None
    loads = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack_from(
            endian + 'IIQQQQQQ', data, off)
        if p_type != 1 or not p_filesz:
            continue
        start = p_paddr or p_vaddr
        loads.append((start, data[p_offset:p_offset + p_filesz]))
        low = start if low is None else min(low, start)
        high = start + p_filesz if high is None else max(high, start + p_filesz)
    if low is None:
        raise SystemExit('ELF has no loadable bytes')
    image = bytearray(high - low)
    for start, blob in loads:
        at = start - low
        image[at:at + len(blob)] = blob
    open(dst, 'wb').write(image)

if __name__ == '__main__':
    main()
