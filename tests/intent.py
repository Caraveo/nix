#!/usr/bin/env python3
"""One typed intent at a time, no overwrite, catalog fetch still denied."""
import tempfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from smoke import VM

def main():
    work = Path(tempfile.mkdtemp(prefix='nix-intent-'))
    disk = work / 'mem.img'
    disk.write_bytes(b'\0' * (4 * 1024 * 1024))
    vm = VM(disk, work)
    vm.send('help', 'You can:')
    vm.send('fetch ubuntu', 'Network denied. The catalog is local.')
    vm.send('ask ubuntu', 'NIX is not Linux.')
    vm.send('n', 'Build cancelled.')
    vm.send('net', 'No network device is attached.')
    vm.send('status', 'dios is root.')
    vm.send('open terminal', 'ghost /memories > ')
    vm.send('whoami', 'dios is root.')
    vm.send('exit', 'nix > ')
    vm.send('intent install ubuntu', 'Install ubuntu as a pod? [y/n]')
    vm.send('intent install windows 11', 'Please answer y or n.')
    vm.send('y', 'Confirm as dios? [y/n]')
    vm.send('y', 'Pod ubuntu recorded.')
    vm.send('intent install ubuntu', 'It was not overwritten.')
    vm.send('intent install windows 11', 'Install windows 11 as a pod? [y/n]')
    vm.send('n', 'Intent cancelled.')
    vm.close()
    print('PASS: one intent, no overwrite, fetch still denied')


if __name__ == '__main__':
    main()
