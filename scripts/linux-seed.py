#!/usr/bin/env python3
"""Fresh host CSPRNG seeds, consumed once by each Linux instance."""
import os
import sys
from pathlib import Path

Path(sys.argv[1]).write_bytes(b'NIXRNG01' + os.urandom(64 * 1024))
