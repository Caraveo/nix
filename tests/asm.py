#!/usr/bin/env python3
"""ask kernel targets Linux 7.3.1. NIX is not Linux. Distro asks boot a stack."""
import tempfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from smoke import VM

def main():
    work = Path(tempfile.mkdtemp(prefix='nix-asm-'))
    disk = work / 'mem.img'
    disk.write_bytes(b'\0' * (4 * 1024 * 1024))
    vm = VM(disk, work)
    vm.send('ask kernel', 'NIX is not Linux.')
    built = vm.send('y', 'It was not started.')
    assert b'Linux 7.3.1 from kernel.org' in built
    assert b'NIX_BUILD_KERNEL linux-7.3.1' in built
    assert b'Opening #kernel.' in built
    vm.send('ask kernel', 'It was not overwritten.')
    vm.send('ask kernel again', 'as #kernel0')
    again = vm.send('y', 'It was not started.')
    assert b'Opening #kernel0.' in again
    vm.send('ask kernel -n linux', 'as #linux')
    named = vm.send('y', 'It was not started.')
    assert b'Opening #linux.' in named
    vm.send('ask kernel -n linux', 'It was not overwritten.')
    vm.send('asm self', 'Assembler self-check passed.')
    vm.send('ask debian', 'NIX is not Linux.')
    deb = vm.send('y', 'NIX_BOOT_GUEST debian')
    assert b'Debian GNU userspace' in deb
    vm.send('ask debian', 'It was not overwritten.')
    vm.send('ask bsd', 'BSD is not Linux 7.3.1.')
    vm.send('ask centos', 'NIX is not Linux.')
    cen = vm.send('y', 'NIX_BOOT_GUEST centos')
    assert b'CentOS GNU userspace' in cen
    vm.close()
    print('PASS: NIX is not Linux, guest kernel is 7.3.1, stacks request boot')

if __name__ == '__main__':
    main()
