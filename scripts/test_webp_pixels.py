#!/usr/bin/env python3
"""Replay owned WebP samples against pinned independent-decoder pixels."""
import base64
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import zlib

binary = str(pathlib.Path(sys.argv[1]).resolve())
fixture = pathlib.Path(__file__).resolve().parents[1] / 'tests/fixtures/vp8-reference.json'
rows = json.loads(fixture.read_text())['cases']
with tempfile.TemporaryDirectory(prefix='cupidimage-vp8-') as directory:
    path = pathlib.Path(directory) / 'owned.webp'
    for row in rows:
        data = (fixture.parents[2] / row['asset']).read_bytes()
        assert hashlib.sha256(data).hexdigest() == row['sha256']
        expected = zlib.decompress(base64.b64decode(row['rgba_zlib'], validate=True))
        assert len(expected) == row['width'] * row['height'] * 4
        path.write_bytes(data)
        result = subprocess.run([binary, str(path), str(row['width']), str(row['height'])],
                                capture_output=True, timeout=10)
        assert result.returncode == 0, (row['name'], result.stderr)
        assert result.stdout == expected, row['name']
print(f'WebP VP8 independent pixels: {len(rows)} images passed')
