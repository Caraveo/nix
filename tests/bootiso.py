#!/usr/bin/env python3
"""The PC disc boots NIX. No -kernel. The Mac is not the machine."""
import tempfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from smoke import VM


def main():
    iso = ROOT / 'build' / 'NIX-0.3-pc.iso'
    if not iso.is_file():
        raise SystemExit('missing build/NIX-0.3-pc.iso')
    work = Path(tempfile.mkdtemp(prefix='nix-iso-'))
    # VM always passes -kernel. Boot the disc instead, then reuse expect().
    vm = VM(None, work, kernel=False, boot_timeout=60, extra=[
        '-nic', 'none', '-boot', 'd', '-cdrom', str(iso),
    ])
    # The constructor already waited for the prompt.
    vm.send('status', 'dios is root.')
    vm.send('help', 'NIX is not Linux.')
    vm.close()
    print('PASS: bootable ISO reached the NIX prompt')


if __name__ == '__main__':
    main()
