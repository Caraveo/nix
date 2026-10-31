#!/usr/bin/env python3
"""Download pinned model sources or verify the prepared offline boot bundle."""
import argparse, hashlib, json, os
from pathlib import Path
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
DIRECTORY=ROOT/'models'
REVISION='12fd25f77366fa6b3b4b768ec3050bf629380bac'
BASE=f'https://huggingface.co/HuggingFaceTB/SmolLM2-135M-Instruct/resolve/{REVISION}/'


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        while chunk:=stream.read(1024*1024):h.update(chunk)
    return h.hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['download','verify'])
    args=parser.parse_args()
    expected=json.loads((DIRECTORY/'sources.json').read_text())
    if args.action=='verify':
        manifest=json.loads((DIRECTORY/'manifest.json').read_text())
        path=DIRECTORY/'smollm2-135m-core-q8.bin'
        if not path.is_file() or path.stat().st_size!=manifest['bundle_bytes'] or digest(path)!=manifest['bundle_sha256']:
            raise SystemExit('Model bundle missing or checksum mismatch. Run ./scripts/setup-model.sh.')
        print('Local AI bundle verified (SHA-256).')
        return
    for name,wanted in expected.items():
        path=DIRECTORY/name
        if path.exists():
            if digest(path)!=wanted:raise SystemExit(f'{name}: existing file checksum mismatch; move it aside before retrying.')
            print(f'{name}: verified',flush=True);continue
        temporary=path.with_suffix(path.suffix+'.part')
        request=urllib.request.Request(BASE+name,headers={'User-Agent':'NIX-Core-model-preparation/1.0'})
        print(f'Downloading {name} from pinned revision {REVISION}',flush=True)
        with urllib.request.urlopen(request,timeout=60) as response,temporary.open('wb') as out:
            while chunk:=response.read(1024*1024):out.write(chunk)
        if digest(temporary)!=wanted:
            temporary.unlink(missing_ok=True);raise SystemExit(f'{name}: download checksum mismatch')
        os.replace(temporary,path)
    print('All pinned model sources verified.')


if __name__=='__main__':main()
