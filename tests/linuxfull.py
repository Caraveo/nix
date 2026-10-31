#!/usr/bin/env python3
"""Real programs and networking inside the Linux intent, not a Mac shell."""
import hashlib
import os
import http.server
import tempfile
import threading
import time
from pathlib import Path
from smoke import VM, ROOT


def runtime_args(work):
    rootfs = ROOT / 'build/linux/userspace.cpio.gz'
    if not rootfs.is_file():
        return None
    seed = work / 'random.bin'
    seed.write_bytes(b'NIXRNG01' + os.urandom(64 * 1024))
    args = ['-device', f'loader,file={rootfs},addr=0xb0000000,force-raw=on',
            '-device', f'loader,file={seed},addr=0xbf000000,force-raw=on']
    for i in range(4):
        args += ['-netdev', f'user,id=intent{i}', '-device',
                 f'ne2k_pci,netdev=intent{i},mac=52:54:00:4e:49:0{i}']
    return args


def main():
    rootfs = ROOT / 'build/linux/userspace.cpio.gz'
    assert rootfs.is_file(), 'Run sh scripts/linux-userspace.sh first'
    payload = b'NIX Linux intent network verification\n' * 400

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header('Content-Length', str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='nix-full-') as tmp:
            work = Path(tmp)
            disk = work / 'mem.img'
            disk.write_bytes(b'\0' * (4 * 1024 * 1024))
            vm = VM(disk, work, memory='8G', extra=runtime_args(work),
                    serial_log=ROOT / 'build/linux/full-serial.log')
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

            try:
                vm.send('ask kernel', 'NIX is not Linux.')
                boot = vm.send('y', 'linux > ', timeout=180)
                assert b'Linux version 7.3.1' in boot
                assert b'Full Linux userspace.' in boot
                assert b'Guest network: ready' in boot
                print('Booted full Linux userspace', flush=True)
                vm.send('id -un', 'dios')
                vm.expect('linux > ')
                vm.send('python3 -c "import math; print(math.sqrt(81))"', '9.0')
                vm.expect('linux > ')
                vm.send('python3 -c "import threading;t=threading.Thread(target=lambda:print(60000+987));t.start();t.join()"', '\n60987', timeout=20)
                vm.expect('linux > ')
                vm.send("echo 'int main(){return 0;}' >/tmp/a.c; cc /tmp/a.c -o /tmp/a; /tmp/a && echo CC_OK", '\nCC_OK', timeout=20)
                vm.expect('linux > ')
                vm.send('sleep 1; echo TIMER_OK', '\nTIMER_OK', timeout=20)
                vm.expect('linux > ')
                vm.send('ping -c 1 -W 3 10.0.2.2', '1 received', timeout=20)
                vm.expect('linux > ')
                digest = hashlib.sha256(payload).hexdigest()
                url = f'http://10.0.2.2:{server.server_port}/payload'
                vm.send(f'curl -fsS --max-time 20 {url} -o /tmp/curl.out', 'linux > ', timeout=30)
                vm.send('sha256sum /tmp/curl.out', digest)
                vm.expect('linux > ')
                vm.send(f'wget -q -T 20 -O /tmp/wget.out {url}', 'linux > ', timeout=30)
                vm.send('sha256sum /tmp/wget.out', digest)
                vm.expect('linux > ')
                vm.send('getent ahostsv4 example.com >/tmp/dns && echo DNS_OK', '\nDNS_OK', timeout=30)
                vm.expect('linux > ')
                vm.send('curl -fsSI --max-time 30 https://example.com | head -n 1', '200', timeout=40)
                vm.expect('linux > ')
                vm.send('wget -q -T 20 -O /tmp/page https://example.com; test -s /tmp/page && echo WGET_TLS_OK', '\nWGET_TLS_OK', timeout=30)
                vm.expect('linux > ')
                vm.send('echo ORIGINAL > /root/marker', 'linux > ')
                click(70)
                vm.expect('nix > ')
                vm.send('ask kernel again', 'NIX is not Linux.')
                vm.send('y', 'linux > ', timeout=180)
                vm.send('test ! -f /root/marker && echo PRIVATE_ROOT', '\nPRIVATE_ROOT')
                vm.expect('linux > ')
                vm.send('ip link show eth0', '52:54:00:4e:49:01')
                vm.expect('linux > ')
                vm.send('ping -c 1 -W 3 10.0.2.2', '1 received', timeout=20)
                vm.expect('linux > ')
                click(360)
                vm.send('cat /root/marker', '\nORIGINAL')
                vm.expect('linux > ')
                vm.send('python3 -c "import math; print(math.sqrt(144))"', '\n12.0')
                vm.expect('linux > ')
                vm.send(f'curl -fsS --max-time 20 {url} -o /tmp/resumed.out', 'linux > ', timeout=30)
                vm.send('sha256sum /tmp/resumed.out', digest)
                vm.expect('linux > ')
                vm.command('screendump', {'filename': str(ROOT / 'build/linux-full-app.ppm')})
                vm.send('nix', 'Linux guest stopped. NIX is still the host.')
                click(360)
                vm.send('nix', 'Linux guest stopped. NIX is still the host.')
                vm.send('fetch ubuntu', 'Network denied. The catalog is local.')
                print('PASS: Linux Bash, Python/threads/FPU, timer, ping, curl, wget, DNS, HTTPS and independent resumable tabs')
            except Exception:
                print(vm.output.decode(errors='replace'), flush=True)
                raise
            finally:
                vm.close()
    finally:
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    main()
