#!/usr/bin/env python3
"""Verify named root filesystems across complete NIX/QEMU restarts."""
import tempfile
from pathlib import Path
from smoke import VM, ROOT
from linuxfull import runtime_args

with tempfile.TemporaryDirectory(prefix='nix-persist-', dir='/tmp') as temp:
    work = Path(temp)
    memory = work / 'memories.img'
    memory.write_bytes(bytes(4 * 1024 * 1024))
    store = work / 'instances.img'
    with store.open('wb') as out:
        out.truncate(128 * 1024**3)
    args = runtime_args(work) + ['-drive',
        f'file={store},format=raw,if=none,id=instance-store', '-device',
        'ide-hd,drive=instance-store,bus=ide.1,unit=0,serial=NIX-PIONEER-STORE']
    def launch():
        return VM(memory, work, memory='8G', extra=args,
                  serial_log=ROOT / 'build/linux/persistence-serial.log')
    vm = launch()
    try:
        vm.send('ask kernel -n saved', 'Confirm as dios? [y/n]')
        vm.send('y', 'linux > ', timeout=300)
        vm.send('echo NIX_PERSIST_OK >/etc/nix-persistence-proof; sync', 'linux > ', timeout=60)
        vm.send('nix', 'nix > ', timeout=90)
    finally:
        vm.close()
    vm = launch()
    try:
        vm.send('ask list', '#saved (saved)')
        vm.send('ask kernel -n saved', 'linux > ', timeout=300)
        result = vm.send('cat /etc/nix-persistence-proof', 'linux > ', timeout=60)
        assert b'NIX_PERSIST_OK' in result, result
        vm.send('nix', 'nix > ', timeout=90)
        vm.send('ask delete saved', 'Use ask delete --confirm NAME.')
        vm.send('ask list', '#saved (saved)')
        vm.send('ask delete --confirm saved', 'Saved instance deleted.')
        print('PASS: entire root survives restart; deletion requires explicit confirmation')
    finally:
        vm.close()
