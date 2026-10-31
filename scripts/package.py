#!/usr/bin/env python3
"""Package the verified kernel + local model, with no personal Memory disk."""
from pathlib import Path
import hashlib,json,subprocess,zipfile
root=Path(__file__).resolve().parents[1]
subprocess.run(['python3',str(root/'scripts/model_bundle.py'),'verify'],check=True)
version=(root/'VERSION').read_text().strip()
name=f'nix-core-{version}-intel-qemu'
out=root/'build'/f'{name}.zip'
files=['build/core.elf','VERSION','models/smollm2-135m-core-q8.bin','models/manifest.json','models/sources.json','models/MODEL-CARD.md','models/LICENSE-APACHE-2.0.txt','models/README.md','scripts/run-prebuilt.sh','scripts/qemu.sh','scripts/model_bundle.py','scripts/memory.py','third_party/hack/LICENSE.md']
with zipfile.ZipFile(out,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=5) as archive:
    for file in files:archive.write(root/file,f'{name}/{file}')
    launcher=zipfile.ZipInfo(f'{name}/Run-NIX.command');launcher.external_attr=0o100755<<16
    archive.writestr(launcher,'#!/bin/sh\ncd "$(dirname "$0")" || exit 1\n./scripts/run-prebuilt.sh\n')
    archive.writestr(f'{name}/README.txt',f'''NIX / Core {version} - local AI QEMU image

Requirements: Intel Mac, Python 3, QEMU (brew install qemu).
Double-click Run-NIX.command, or run ./scripts/run-prebuilt.sh.
The prebuilt kernel and SmolLM2-135M-Instruct Q8 boot bundle are included.
The launcher verifies the model and creates a NEW blank Memories disk on first launch.
No personal Memories or source-model download is included in this archive.

Ask a plain-language question from Home. Escape cancels inference.
Use ai status for model details. Explicit writing/Ghost commands still work.
The small model can be wrong; generated advice does not execute OS actions.

Source, instructions, model provenance, and current limitations:
https://github.com/Caraveo/nix

SmolLM2 weights: Apache 2.0, see models/LICENSE-APACHE-2.0.txt.
Hack font attribution: third_party/hack/LICENSE.md.
''')
print(f'{out}\n{out.stat().st_size:,} bytes\nSHA256 {hashlib.sha256(out.read_bytes()).hexdigest()}')
# Personal data must never enter a distributable OS archive.
with zipfile.ZipFile(out) as archive:
    assert all(not n.endswith('memories.img') for n in archive.namelist())
