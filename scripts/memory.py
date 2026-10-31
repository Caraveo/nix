#!/usr/bin/env python3
"""Inspect/export the versioned NIX Memory disk without mounting it."""
import argparse
import pathlib
import struct

SECTOR = 512
BANK_SIZE = 34 * SECTOR
MAGIC = 0x3158494E
DISK_SIZE = 4 * 1024 * 1024


def checksum(data):
    h = 2166136261
    for i, b in enumerate(data):
        h = ((h ^ (0 if 8 <= i < 12 else b)) * 16777619) & 0xFFFFFFFF
    return h


def decode(data):
    if len(data) != BANK_SIZE:
        return None
    magic, generation, digest, count = struct.unpack_from('<4I', data)
    if magic != MAGIC or count > 4 or digest != checksum(data):
        return None
    books = []
    for i in range(count):
        offset = 16 + i * 4148
        title = data[offset:offset+48]
        size, = struct.unpack_from('<I', data, offset+48)
        text = data[offset+52:offset+52+4096]
        if title[-1] or size >= 4096 or text[size]:
            return None
        books.append((title.split(b'\0')[0].decode('ascii'), text[:size].decode('ascii')))
    return generation, books


def read_disk(path):
    with path.open('rb') as stream:
        stream.seek(SECTOR)
        banks = [decode(stream.read(BANK_SIZE)), decode(stream.read(BANK_SIZE))]
    valid = [bank for bank in banks if bank is not None]
    if not valid:
        raise SystemExit('No valid committed Memories. Disk is new or damaged.')
    return max(valid, key=lambda bank: bank[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['init', 'list', 'export'])
    parser.add_argument('disk', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('exports'))
    args = parser.parse_args()
    if args.action == 'init':
        if args.disk.exists():
            if args.disk.stat().st_size != DISK_SIZE:
                raise SystemExit('Unexpected disk size; refusing to modify it.')
        else:
            args.disk.parent.mkdir(parents=True, exist_ok=True)
            with args.disk.open('xb') as stream:
                stream.truncate(DISK_SIZE)
        return
    generation, books = read_disk(args.disk)
    print(f'Committed generation {generation}: {len(books)} Memories')
    for index, (title, text) in enumerate(books, 1):
        print(f'{index}. {title} ({len(text)} bytes)')
        if args.action == 'export':
            args.output.mkdir(parents=True, exist_ok=True)
            slug = ''.join(c if c.isalnum() or c in '-_' else '_' for c in title)
            path = args.output / f'{index:02d}-{slug}.txt'
            # Explicitly refuse to overwrite an earlier export.
            with path.open('x') as stream:
                stream.write(text)
            print(f'   -> {path}')


if __name__ == '__main__':
    main()
