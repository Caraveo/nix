#!/usr/bin/env python3
"""Real in-guest inference with NIC absent, then cancellation and typed recovery."""
from pathlib import Path
import subprocess, tempfile, time
from smoke import VM, ROOT
model=ROOT/'models/smollm2-135m-core-q8.bin'
assert model.exists(),'Prepare the model first: see models/README.md'
subprocess.run([str(ROOT/'scripts/build.sh')],check=True)
with tempfile.TemporaryDirectory(prefix='nix-ai-') as tmp:
    work=Path(tmp);disk=work/'memories.img'
    subprocess.run(['python3',str(ROOT/'scripts/memory.py'),'init',str(disk)],check=True)
    vm=VM(disk,work,model=model)
    try:
        vm.command('screendump',{'filename':str(ROOT/'build/home-ai.ppm')})
        vm.send('ai status','SmolLM2-135M-Instruct / Q8')
        vm.expect('nix > ')
        started=time.monotonic()
        vm.process.stdin.write(b'What is the capital of France?\n');vm.process.stdin.flush()
        vm.expect('Thinking locally.',timeout=20)
        vm.expect('Paris',timeout=180)
        vm.expect('nix > ',timeout=180)
        elapsed=time.monotonic()-started
        print(f'PASS: real QEMU inference answered Paris, {elapsed:.2f}s, NIC absent',flush=True)
        started=time.monotonic()
        vm.process.stdin.write(b'What is the capital of Japan?\n');vm.process.stdin.flush()
        vm.expect('Tokyo',timeout=120);vm.expect('nix > ',timeout=120)
        print(f'PASS: cached-prefix second answer Tokyo, {time.monotonic()-started:.2f}s',flush=True)
        vm.command('screendump',{'filename':str(ROOT/'build/ai.ppm')})
        vm.process.stdin.write(b'ask Explain how a computer works.\n');vm.process.stdin.flush()
        vm.expect('Reading request',timeout=20)
        vm.process.stdin.write(b'\x1b');vm.process.stdin.flush()
        vm.expect('Cancelled.',timeout=20);vm.expect('nix > ')
        vm.send('write a book called AI Test','  + ')
        vm.send('The deterministic editor still works.','  + ')
        vm.send(':save','Saved and verified on disk.')
        vm.expect('  + ')
        vm.send(':boot','Restarting NIX.')
        vm.expect('NIX_BOOT_SPLASH');vm.expect('nix > ')
        vm.send('ai status','SmolLM2-135M-Instruct / Q8')
        print('PASS: inference cancels; Author saves work; model reloads after actual guest reboot',flush=True)
    finally:
        vm.close()
