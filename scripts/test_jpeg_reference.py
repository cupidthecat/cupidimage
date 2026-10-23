#!/usr/bin/env python3
"""Replay real baseline/progressive JPEGs against independent libjpeg pixels."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib

root = Path(__file__).resolve().parents[1]
probe = str(Path(sys.argv[1]).resolve())
rows = json.loads((root / 'tests/fixtures/jpeg-reference.json').read_text())['cases']
for row in rows:
    path = root / row['asset']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256']
    result = subprocess.run([probe, str(path), 'pixels'], capture_output=True, timeout=10)
    assert result.returncode == 0, (path, result.stderr)
    width, height, hx, hy = struct.unpack('=4I', result.stdout[:16])
    assert (width, height, hx, hy) == (row['width'], row['height'], 0, 0)
    actual = result.stdout[16:]
    expected = zlib.decompress(base64.b64decode(row['rgba_zlib'], validate=True))
    assert len(actual) == len(expected) == width * height * 4
    difference = max(abs(a-b) for a,b in zip(actual, expected))
    assert difference <= row['tolerance'], (row['asset'], difference)
print(f'Independent JPEG pixels: {len(rows)} images passed')
