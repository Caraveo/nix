#!/usr/bin/env python3
"""Boot the real kernel; exercise serial, PS/2, save/reboot and bank recovery."""
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from memory import BANK_SIZE, decode, read_disk


class VM:
    def __init__(self, disk, work, model=None, capture_boot=None, extra=None, memory=None,
                 kernel=True, boot_timeout=15, serial_log=None):
        self.serial_log = Path(serial_log) if serial_log else None
        if self.serial_log:
            self.serial_log.write_bytes(b'')
        self.qmp_path = str(work / 'qmp.sock')
        Path(self.qmp_path).unlink(missing_ok=True)
        drive = ['-drive', f'file={disk},format=raw,if=ide,index=0'] if disk else []
        model_args=['-initrd',str(model)] if model else []
        if extra is None:
            extra = ['-nic', 'none']
        if memory is None:
            memory = '512M' if model else '64M'
        self.process = subprocess.Popen([
            str(ROOT/'scripts/qemu.sh'), '-machine', 'pc,accel=tcg', '-cpu', 'phenom',
            '-m', memory,
            '-vga', 'std',
            *(['-kernel', str(ROOT / 'build/core.elf')] if kernel else []),
            *drive, *model_args,
            *extra, '-display', 'none', '-serial', 'stdio',
            '-monitor', 'none', '-qmp', f'unix:{self.qmp_path},server=on,wait=off',
            ], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        self.output = b''
        self.qmp = socket.socket(socket.AF_UNIX)
        deadline=time.monotonic()+10
        while True:
            try:self.qmp.connect(self.qmp_path);break
            except (FileNotFoundError,ConnectionRefusedError):
                if time.monotonic()>deadline:raise
                time.sleep(.01)
        self.stream = self.qmp.makefile('rb')
        self.stream.readline()
        self.command('qmp_capabilities')
        if capture_boot:
            self.expect('NIX_BOOT_SPLASH')
            self.command('screendump',{'filename':str(capture_boot)})
        self.expect('nix > ', boot_timeout)

    def command(self, command, arguments=None):
        request = {'execute': command}
        if arguments is not None:
            request['arguments'] = arguments
        self.qmp.sendall(json.dumps(request).encode() + b'\n')
        while True:
            response = json.loads(self.stream.readline())
            if 'error' in response:
                raise AssertionError(response)
            if 'return' in response:
                return response['return']

    def expect(self, text, timeout=15):
        target = text.encode()
        deadline = time.monotonic() + timeout
        while target not in self.output:
            if time.monotonic() > deadline:
                raise AssertionError(f'Timeout waiting for {text!r}: {self.output!r}')
            if select.select([self.process.stdout], [], [], .1)[0]:
                chunk = os.read(self.process.stdout.fileno(), 8192)
                if not chunk:
                    raise AssertionError(self.process.stderr.read().decode())
                self.output += chunk
                if self.serial_log:
                    with self.serial_log.open('ab') as log:
                        log.write(chunk)
                if b'Kernel panic - not syncing:' in self.output:
                    raise AssertionError('Linux guest panicked; see captured serial output')
        before, self.output = self.output.split(target, 1)
        return before

    def send(self, text, expected, timeout=15):
        self.process.stdin.write((text + '\n').encode())
        self.process.stdin.flush()
        return self.expect(expected, timeout)

    def close(self):
        self.process.terminate()
        self.process.communicate(timeout=5)
        self.stream.close()
        self.qmp.close()


def main():
    subprocess.run([str(ROOT/'scripts/qemu.sh'), '--version'], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([str(ROOT / 'scripts/build.sh')], check=True)
    with tempfile.TemporaryDirectory(prefix='nix-test-') as tmp:
        work = Path(tmp)
        disk = work / 'memories.img'
        subprocess.run([sys.executable, str(ROOT/'scripts/memory.py'), 'init', str(disk)], check=True)
        vm = VM(disk, work)
        try:
            vm.command('screendump', {'filename': str(ROOT/'build/home.ppm')})
            assert (ROOT/'build/home.ppm').read_bytes().startswith(b'P6\n1024 768\n255\n')
            def frame_pixel(path, x, y):
                data=path.read_bytes().split(b'\n',3)[3]
                offset=(y*1024+x)*3
                return tuple(data[offset:offset+3])
            capture=ROOT/'build/home.ppm'
            assert frame_pixel(capture,16,24)==(33,23,19), 'Home is not the dark field'
            assert frame_pixel(capture,944,632)==(226,154,118), 'Prompt button missing'
            assert frame_pixel(capture,900,700)==(23,23,36), 'Pointer missing'
            def move(dx,dy):
                vm.command('input-send-event', {'events':[
                    {'type':'rel','data':{'axis':'x','value':dx}},
                    {'type':'rel','data':{'axis':'y','value':dy}}]})
                time.sleep(.06)
            move(60,40)
            vm.command('screendump', {'filename':str(capture)})
            assert frame_pixel(capture,960,740)==(23,23,36), 'Mouse did not move'
            assert frame_pixel(capture,900,700)==(33,23,19), 'Pointer left trails'
            # The home face has no toolbar. The prompt opens Ghost.
            vm.send('open terminal', 'ghost /memories > ')
            vm.send('exit','nix > ')
            vm.command('screendump', {'filename':str(capture)})
            assert frame_pixel(capture,944,632)==(226,154,118), 'Home prompt did not return'
            # Input through the emulated keyboard, not serial.
            for key in ['h', 'e', 'l', 'p', 'ret']:
                vm.command('human-monitor-command', {'command-line': f'sendkey {key}'})
                time.sleep(.08)
            vm.expect('You can:')
            vm.expect('nix > ')
            vm.send('write a book called First Light', '  + ')
            vm.send('The first words survive a reboot.', '  + ')
            vm.send(':save', 'Saved and verified on disk.')
            vm.expect('  + ')
            vm.send('This is the second committed line.', '  + ')
            vm.send(':save', 'Saved and verified on disk.')
            vm.expect('  + ')
            vm.send('Unsaved words must not survive.', '  + ')
        finally:
            vm.close()
        generation, books = read_disk(disk)
        assert generation == 2
        assert books == [('First Light', 'The first words survive a reboot.\nThis is the second committed line.\n')]
        vm = VM(disk, work)
        try:
            vm.send('open First Light', '  + ')
            vm.send(':read', 'This is the second committed line.')
            vm.expect('  + ')
            vm.send(':home', 'nix > ')
            vm.send('show my memories', 'First Light')
            vm.expect('nix > ')
            vm.send('open terminal', 'ghost /memories > ')
            vm.send('echo "Ghost is here" && pwd', '/memories')
            vm.expect('ghost /memories > ')
            vm.send('missing-command || echo recovered', 'recovered')
            vm.expect('ghost /memories > ')
            vm.send('echo "Terminal append" >> "First Light"', 'Memory appended, saved, and verified.')
            vm.expect('ghost /memories > ')
            vm.send('cat "First Light"', 'Terminal append')
            vm.expect('ghost /memories > ')
            vm.send('echo unsafe | cat', 'no pipes')
            vm.expect('ghost /memories > ')
            vm.send('echo "broken', 'unterminated quote')
            vm.expect('ghost /memories > ')
            vm.send('clear', 'ghost /memories > ')
            vm.send('help', 'ghost /memories > ')
            screenshot = ROOT / 'build/ghost.ppm'
            vm.command('screendump', {'filename': str(screenshot)})
        finally:
            vm.close()
        generation, books = read_disk(disk)
        assert generation == 3 and books[0][1].endswith('Terminal append\n')
        export = work/'exports'
        subprocess.run([sys.executable, str(ROOT/'scripts/memory.py'), 'export', str(disk), '--output', str(export)], check=True)
        assert (export/'01-First_Light.txt').read_text() == books[0][1]
        # Corrupt newest bank; the kernel must recover the previous generation.
        with disk.open('r+b') as stream:
            stream.seek(512)
            banks = [stream.read(BANK_SIZE), stream.read(BANK_SIZE)]
            newest = max(range(2), key=lambda i: decode(banks[i])[0])
            stream.seek(512 + newest * BANK_SIZE + 600)
            stream.write(b'CORRUPT!')
        vm = VM(disk, work)
        try:
            vm.send('open First Light', '  + ')
            content = vm.send(':read', '  + ')
            assert b'The first words survive a reboot.' in content
            assert b'This is the second committed line.' in content
            assert b'Terminal append' not in content
            vm.send(':home', 'nix > ')
            vm.send('shutdown', '[y/n] > ')
            vm.send('y', 'Goodbye.')
            vm.process.wait(timeout=5)
            assert vm.process.returncode == 0
        finally:
            vm.close()
        # With no attached disk, boot still works and saves fail visibly.
        vm = VM(None, work)
        try:
            vm.send('write a book', '  + ')
            vm.send('Hello', '  + ')
            vm.send(':save', 'Save failed. Text remains in RAM')
        finally:
            vm.close()
    print('PASS: boot, dark welcome, PS/2 mouse movement/no trails, PS/2 keyboard, Author, disk save, restart, unsaved isolation, bank recovery, Ghost chains/quotes/append/errors, shutdown, missing disk')


if __name__ == '__main__':
    main()
