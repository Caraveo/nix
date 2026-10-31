#!/usr/bin/env python3
"""24 visible rows, wheel/keyboard scrollback, and per-tab history."""
import hashlib
import tempfile
import time
from pathlib import Path
from smoke import VM, ROOT
from linuxfull import runtime_args


def main():
    with tempfile.TemporaryDirectory(prefix='nix-scroll-') as tmp:
        work = Path(tmp)
        disk = work / 'mem.img'
        disk.write_bytes(b'\0' * (4 * 1024 * 1024))
        vm = VM(disk, work, memory='8G', extra=runtime_args(work))
        pointer = [900, 700]

        def move(x, y):
            dx, dy = x - pointer[0], y - pointer[1]
            while dx or dy:
                sx, sy = max(-80, min(80, dx)), max(-80, min(80, dy))
                vm.command('input-send-event', {'events': [
                    {'type': 'rel', 'data': {'axis': 'x', 'value': sx}},
                    {'type': 'rel', 'data': {'axis': 'y', 'value': sy}}]})
                time.sleep(.04)
                dx, dy = dx - sx, dy - sy
            pointer[:] = [x, y]

        def button(name):
            for down in (True, False):
                vm.command('input-send-event', {'events': [
                    {'type': 'btn', 'data': {'button': name, 'down': down}}]})
                time.sleep(.07)

        def shot(name):
            p = ROOT / 'build' / f'console-{name}.ppm'
            vm.command('screendump', {'filename': str(p)})
            raw = p.read_bytes().split(b'\n', 3)[3]
            content = b''.join(raw[(y * 1024 + 36) * 3:(y * 1024 + 880) * 3]
                               for y in range(132, 588))
            return raw, hashlib.sha256(content).digest()

        try:
            vm.send('ask kernel', 'NIX is not Linux.')
            vm.send('y', 'linux > ', timeout=180)
            vm.send('seq -f ROW%03g 1 80', 'ROW080')
            vm.expect('linux > ')
            move(900, 400)
            button('wheel-down')
            raw, live = shot('live')
            bg = bytes((30, 23, 19))
            occupied = sum(any(raw[(y * 1024 + x) * 3:(y * 1024 + x) * 3 + 3] != bg
                               for y in range(132 + r * 19, 150 + r * 19)
                               for x in range(36, 280)) for r in range(24))
            assert occupied == 24, occupied
            button('wheel-up')
            _, older = shot('older')
            assert older != live, 'Wheel did not scroll the console'
            move(70, 86)
            button('left')
            vm.expect('nix > ')
            move(360, 86)
            button('left')
            _, restored = shot('restored')
            assert restored == older, 'Tab switch lost its scroll position'
            vm.command('human-monitor-command', {'command-line': 'sendkey shift-pgup'})
            time.sleep(.2)
            _, paged = shot('page-up')
            assert paged != older, 'Shift+PageUp did not scroll'
            move(984, 587)
            button('left')
            _, bottom = shot('bottom')
            assert bottom == live, 'Scrollbar did not return to the latest output'
            vm.send('nix', 'Linux guest stopped. NIX is still the host.')
            print('PASS: 24 rows, mouse wheel, Shift+PageUp, scrollbar and independent tab scrollback')
        finally:
            vm.close()


if __name__ == '__main__':
    main()
