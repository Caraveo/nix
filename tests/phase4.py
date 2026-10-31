#!/usr/bin/env python3
"""Phase 4: dios confirms an SVM build, the guest cannot see Memories, rollback works."""
import subprocess
import sys
import tempfile
from pathlib import Path

from smoke import ROOT, VM

sys.path.insert(0, str(ROOT / 'scripts'))
from memory import read_disk

subprocess.run([str(ROOT / 'scripts/build.sh')], check=True)
with tempfile.TemporaryDirectory(prefix='nix-phase4-') as tmp:
    work = Path(tmp)
    disk = work / 'memories.img'
    subprocess.run([sys.executable, str(ROOT / 'scripts/memory.py'), 'init', str(disk)], check=True)
    vm = VM(disk, work)
    try:
        vm.send('write a book called Secret Manuscript', '  + ')
        vm.send('UniqueSecretPhrase stays on disk.', '  + ')
        vm.send(':undo', 'Last line removed in RAM.')
        vm.send('UniqueSecretPhrase stays on disk.', '  + ')
        vm.send(':save', 'Saved and verified on disk.')
        vm.expect('  + ')
        vm.send(':home', 'nix > ')
        vm.send('fetch https://example.com/source', 'Network denied. The catalog is local.')
        vm.expect('nix > ')
        vm.send('build nope', 'Unknown recipe. The catalog has count-words.')
        vm.expect('nix > ')
        vm.send('build', 'Which recipe should be built?')
        vm.send('count-words', 'Confirm as dios? [y/n]')
        vm.send('n', 'Build cancelled.')
        vm.expect('nix > ')
        plan = vm.send('build probe-fault', 'Confirm as dios? [y/n]')
        assert b'network: denied' in plan
        assert b'memories: none' in plan
        assert b'authority: dios' in plan
        assert b'UniqueSecretPhrase' not in plan
        probed = vm.send('y', 'Nested page fault. Guest stopped. Memories unchanged.')
        assert b'Hypervisor: AMD SVM with nested paging. Guest map is 2 MiB.' in probed
        assert b'UniqueSecretPhrase' not in probed
        vm.expect('nix > ')
        vm.send('show my memories', 'Secret Manuscript')
        vm.expect('nix > ')
        vm.send('build count-words grant Missing Book', 'Memory not found. The build was not started.')
        vm.expect('nix > ')
        vm.send('build count-words', 'Confirm as dios? [y/n]')
        built = vm.send('y', 'Installed count-words version 1.')
        assert b'Hypervisor: AMD SVM with nested paging. Guest map is 2 MiB.' in built
        assert b'UniqueSecretPhrase' not in built
        vm.expect('Manifest ')
        vm.expect('nix > ')
        vm.send('build count-words', 'Nothing was built.')
        vm.expect('nix > ')
        denied = vm.send('run counter', 'counter > ')
        assert b'Memory grant: denied.' in denied
        assert b'UniqueSecretPhrase' not in denied
        vm.send('one two three', '3')
        vm.expect('nix > ')
        vm.send('pod fault', 'Pod faulted. NIX is still running.')
        vm.expect('nix > ')
        vm.send('help', 'You can:')
        vm.expect('nix > ')
        vm.send('rebuild count-words', 'Confirm as dios? [y/n]')
        vm.send('y', 'Installed count-words version 2.')
        vm.expect('nix > ')
        vm.send('rollback counter', 'Rolled back to the previous pod.')
        vm.expect('nix > ')
        vm.send('build count-words grant Secret Manuscript', 'Confirm as dios? [y/n]')
        vm.send('y', 'Nothing was built.')
        vm.expect('Memory grant: Secret Manuscript')
        vm.expect('The build guest did not receive it.')
        vm.expect('nix > ')
        counted = vm.send('run counter', 'nix > ')
        assert b'Memory grant: denied.' not in counted
        assert b'UniqueSecretPhrase' not in counted
        assert b'\n4\n' in counted
        vm.send('boot', 'Restarting NIX.')
        vm.expect('NIX_BOOT_SPLASH')
        vm.expect('nix > ')
        vm.send('build count-words', 'Nothing was built.')
        vm.expect('nix > ')
        vm.send('open Secret Manuscript', '  + ')
        vm.send(':read', 'UniqueSecretPhrase stays on disk.')
        vm.expect('  + ')
        vm.send(':home', 'nix > ')
    finally:
        vm.close()
    _generation, books = read_disk(disk)
    assert books[0] == ('Secret Manuscript', 'UniqueSecretPhrase stays on disk.\n')
print('PASS: dios-confirmed SVM build, memory isolation, reuse, rollback, pod fault')
