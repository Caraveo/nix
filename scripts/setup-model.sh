#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 scripts/model_bundle.py download
if [ ! -x models/.venv/bin/python ]; then python3 -m venv models/.venv; fi
models/.venv/bin/python -m pip install 'numpy>=2.0,<3' 'tokenizers>=0.20,<1'
models/.venv/bin/python scripts/prepare_model.py
python3 scripts/model_bundle.py verify
