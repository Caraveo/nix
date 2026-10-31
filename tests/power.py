#!/usr/bin/env python3
"""Actual guest shutdown confirmation, restart, artwork, menu and draft safety."""
import subprocess,sys,tempfile,time
from pathlib import Path
from smoke import VM,ROOT
sys.path.insert(0,str(ROOT/'scripts'))
from memory import read_disk
subprocess.run([str(ROOT/'scripts/build.sh')],check=True)

def pixel(path,x,y):
    data=path.read_bytes().split(b'\n',3)[3];offset=(y*1024+x)*3
    return tuple(data[offset:offset+3])

def move(vm,dx,dy):
    while dx or dy:
        x=max(-80,min(80,dx));y=max(-80,min(80,dy))
        vm.command('input-send-event',{'events':[{'type':'rel','data':{'axis':'x','value':x}},{'type':'rel','data':{'axis':'y','value':y}}]})
        time.sleep(.04);dx-=x;dy-=y

def click(vm):
    for down in [True,False]:
        vm.command('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':down}}]})
        time.sleep(.06)

with tempfile.TemporaryDirectory(prefix='nix-power-') as tmp:
    work=Path(tmp);disk=work/'memories.img'
    subprocess.run(['python3',str(ROOT/'scripts/memory.py'),'init',str(disk)],check=True)
    vm=VM(disk,work,capture_boot=ROOT/'build/boot.ppm')
    try:
        assert pixel(ROOT/'build/boot.ppm',512,384)==(0,0,0),'Centered black emblem missing'
        assert pixel(ROOT/'build/boot.ppm',200,384)==(250,250,252),'Unexpected splash background'
        # Real pointer click opens bottom-left NIX menu, then selects Ghost.
        move(vm,51-720,722-440);click(vm)
        vm.command('screendump',{'filename':str(ROOT/'build/menu.ppm')})
        assert pixel(ROOT/'build/menu.ppm',58,466)==(0,0,0),'Menu emblem missing'
        move(vm,140-51,575-722);click(vm)
        vm.expect('ghost /memories > ')
        vm.send('die ; boot','[y/n] > ')
        vm.send('maybe','[y/n] > ')
        assert vm.process.poll() is None
        vm.send('n','ghost /memories > ')
        vm.send('boot','Restarting NIX.')
        vm.expect('NIX_BOOT_SPLASH');vm.expect('nix > ')
        assert vm.process.poll() is None,'boot exited QEMU instead of restarting'
        vm.send('write a book called Reboot Story','  + ')
        vm.send('This unsaved draft must survive boot.','  + ')
        vm.send(':boot','Restarting NIX.')
        vm.expect('NIX_BOOT_SPLASH');vm.expect('nix > ')
        vm.send('open Reboot Story','  + ')
        vm.send(':read','This unsaved draft must survive boot.');vm.expect('  + ')
        vm.send('This line must survive die.','  + ')
        vm.send(':die','[y/n] > ')
        vm.send('n','  + ')
        vm.send(':die','[y/n] > ')
        vm.send('y','Goodbye.')
        vm.process.wait(timeout=5);assert vm.process.returncode==0
    finally:vm.close()
    _,books=read_disk(disk)
    assert books[0][1]=='This unsaved draft must survive boot.\nThis line must survive die.\n'
    vm=VM(None,work)
    try:
        vm.send('write a book called RAM Draft','  + ')
        vm.send('Keep this draft.','  + ')
        vm.send(':die','[y/n] > ')
        vm.send('y','Power action cancelled');vm.expect('  + ')
        vm.send(':boot','Power action cancelled');vm.expect('  + ')
        assert vm.process.poll() is None
        vm.send(':read','Keep this draft.')
    finally:vm.close()
print('PASS: black centered splash, NIX menu mouse launch, die y/n, actual reboot, autosave, failure preservation')
