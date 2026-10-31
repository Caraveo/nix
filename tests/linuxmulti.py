#!/usr/bin/env python3
"""Four real Linux guests: mouse switching, saved input, capacity and close."""
import tempfile
import time
from pathlib import Path
from smoke import VM
from linuxfull import runtime_args


def main():
    with tempfile.TemporaryDirectory(prefix='nix-multi-') as tmp:
        work = Path(tmp)
        disk = work / 'mem.img'
        disk.write_bytes(b'\0' * (4 * 1024 * 1024))
        extra = runtime_args(work)
        full = extra is not None
        prompt = 'linux > ' if full else 'linux > \n'
        vm = VM(disk, work, memory='8G', extra=extra)
        pointer = [900, 700]

        def click(x, y=86):
            dx, dy = x - pointer[0], y - pointer[1]
            while dx or dy:
                sx, sy = max(-80, min(80, dx)), max(-80, min(80, dy))
                vm.command('input-send-event', {'events': [
                    {'type': 'rel', 'data': {'axis': 'x', 'value': sx}},
                    {'type': 'rel', 'data': {'axis': 'y', 'value': sy}}]})
                time.sleep(.04)
                dx, dy = dx - sx, dy - sy
            pointer[:] = [x, y]
            for down in (True, False):
                vm.command('input-send-event', {'events': [
                    {'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
                time.sleep(.06)

        def partial(text):
            vm.process.stdin.write(text.encode())
            vm.process.stdin.flush()
            time.sleep(.15)

        try:
            asks = ['ask kernel', 'ask kernel again',
                    'ask kernel -n linux', 'ask kernel again']
            names = ['kernel', 'kernel0', 'linux', 'kernel1']
            # Leave a distinct, unfinished command in each guest's shell RAM.
            for i, (ask, name) in enumerate(zip(asks, names)):
                vm.send(ask, 'NIX is not Linux.')
                boot = vm.send('y', prompt, timeout=300)
                assert f'Opening #{name}.'.encode() in boot
                assert b'Linux version 7.3.1' in boot
                print(f'Booted #{name}', flush=True)
                partial(f'echo saved{i}')
                click(70)
                vm.expect('nix > ')
            vm.send('ask kernel again', 'NIX is not Linux.')
            vm.send('y', 'Four kernels are already running. Stop one with nix.')
            vm.expect('nix > ')
            # Resume directly from guest to guest, including backwards jumps.
            centers = [360, 484, 608, 732]
            for i in (2, 0, 3, 1):
                click(centers[i])
                vm.send('', f'saved{i}\n{prompt}')
                print(f'Resumed #{names[i]} with its own input', flush=True)
            click(210)
            vm.expect('ghost /memories > ')
            click(70)
            vm.expect('nix > ')
            vm.send('status', '4 Linux kernels.')
            # Close one tab only; the others must remain resumable.
            click(centers[0])
            vm.send('nix', 'Linux guest stopped. NIX is still the host.')
            vm.send('status', '3 Linux kernels.')
            click(360)  # kernel0 moved left after kernel closed.
            vm.send('uname -r' if full else 'uname',
                    '7.3.1' if full else 'Linux 7.3.1 x86_64. NIX is the host.')
            print('PASS: four Linux tabs, independent saved input, switching, limit and close')
        finally:
            vm.close()


if __name__ == '__main__':
    main()
