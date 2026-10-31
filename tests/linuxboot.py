#!/usr/bin/env python3
"""Linux 7.3.1 inside NIX. Dynamic RAM and disk. NIX stays the host.

A one-byte guest image must refuse quickly. A real bzImage must print its
own version and stay at linux > until exit. The 2 MiB build guest is not this VM.
"""
import tempfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from smoke import VM
from linuxfull import runtime_args


def main():
    work = Path(tempfile.mkdtemp(prefix='nix-linux-'))
    disk = work / 'mem.img'
    disk.write_bytes(b'\0' * (4 * 1024 * 1024))
    image = ROOT / 'build' / 'guest-bzImage'
    real = image.is_file() and image.stat().st_size > 0x10000
    extra = runtime_args(work)
    full = extra is not None
    vm = VM(disk, work, memory='8G', extra=extra)
    # RAM above the PCI hole must count toward the host requirement.
    vm.send('status', 'Memory: 8192 MiB. NIX requires 8 GiB.')
    vm.send('ask debian', 'NIX is not Linux.')
    if not real:
        vm.send('y', 'image is not loaded.', timeout=20)
        vm.send('status', 'dios is root.')
        vm.close()
        print('PASS: placeholder image was not started')
        return
    before = vm.send('y', 'linux > ' if full else 'linux > \n', timeout=300)
    assert b'Linux version 7.3.1' in before
    assert b'Dynamic RAM:' in before
    assert b'Dynamic disk:' in before
    assert (b'Full Linux userspace.' if full else b'Guest disk') in before
    assert b'Linux guest stopped' not in before
    shot = ROOT / 'build' / 'debian-app.ppm'
    vm.command('screendump', {'filename': str(shot)})
    raw = shot.read_bytes().split(b'\n', 3)[3]
    def pix(x, y):
        o = (y * 1024 + x) * 3
        return tuple(raw[o:o + 3])
    # #debian sits to the right of Terminal and is the selected app.
    assert pix(364, 68) == (74, 56, 46), pix(364, 68)
    assert pix(364, 86) == (54, 39, 32), pix(364, 86)
    assert pix(960, 450) == (30, 23, 19), pix(960, 450)
    # The boot log is longer than the console. The latest lines must stay.
    ink = sum(1 for y in range(140, 400) for x in range(40, 420)
              if pix(x, y) != (30, 23, 19))
    assert ink > 200, ink
    vm.send('uname -r' if full else 'uname',
            '7.3.1' if full else 'Linux 7.3.1 x86_64. NIX is the host.')
    vm.send('echo hello', 'hello\nlinux')
    vm.send('nix' if full else 'exit', 'NIX is still the host.')
    vm.send('status', 'dios is root.')
    vm.send('status', '2 MiB build guest is separate.')
    vm.close()
    print('PASS: Linux 7.3.1 ran under NIX with dynamic RAM and disk')


if __name__ == '__main__':
    main()
