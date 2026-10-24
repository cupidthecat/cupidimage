#!/usr/bin/env python3
"""Check real assets through every decoder and all three terminal renderers."""
import base64
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import zlib
from test_sixel import decode as decode_sixel
from terminal_streams import decode_ansi

ROOT = Path(__file__).resolve().parents[1]
probe = str(Path(sys.argv[1]).resolve())
rows = json.loads((ROOT / 'tests/fixtures/format-reference.json').read_text())['cases']
assert {Path(row['asset']).suffix for row in rows} == {'.png', '.bmp', '.gif', '.tga', '.tif', '.webp', '.ico', '.cur', '.jpg', '.svg', '.pdf'}
assert len(rows) == len({row['asset'] for row in rows})


def run(path, mode):
    result = subprocess.run([probe, str(path), mode], capture_output=True, timeout=15)
    assert result.returncode == 0, (path, mode, result.stderr)
    return result.stdout




for row in rows:
    path = ROOT / row['asset']
    expected = base64.b64decode(row['rgba'], validate=True)
    raw = run(path, 'pixels')
    width, height, hx, hy = struct.unpack('=4I', raw[:16])
    actual = raw[16:]
    assert (width, height) == (row['width'], row['height']), path
    assert [hx, hy] == row.get('hotspot', [0, 0]), path
    assert len(actual) == len(expected) == width * height * 4
    assert max(abs(a-b) for a, b in zip(actual, expected)) <= row['tolerance'], path
    rgb = [tuple(actual[i:i+3]) for i in range(0, len(actual), 4)]
    assert all(actual[i] == 255 for i in range(3, len(actual), 4))

    widths, ansi = decode_ansi(run(path, 'ansi'))
    assert widths == [width] * height and ansi == rgb, (path, 'ANSI round trip')

    data = run(path, 'kitty')
    chunks = re.findall(rb'\x1b_G([^\x1b]*)\x1b\\', data)
    assert b''.join(b'\x1b_G' + c + b'\x1b\\' for c in chunks) + b'\n' == data
    encoded = bytearray()
    for i, chunk in enumerate(chunks):
        control, payload = chunk.split(b';', 1)
        fields = dict(field.split(b'=', 1) for field in control.split(b','))
        if i == 0:
            first = fields
        assert fields[b'm'] == (b'0' if i == len(chunks)-1 else b'1')
        assert len(payload) <= 4096 and len(payload) % 4 == 0
        encoded.extend(payload)
    assert first[b's'] == str(width).encode() and first[b'v'] == str(height).encode()
    decoded = base64.b64decode(encoded, validate=True)
    if first.get(b'o') == b'z': decoded = zlib.decompress(decoded)
    assert decoded == actual, (path, 'Kitty round trip')

    transparent, dimensions, sixel = decode_sixel(run(path, 'sixel'))
    assert not transparent and dimensions == (width, height)
    assert sixel == [tuple(c * 100 // 255 for c in px) for px in rgb], (path, 'Sixel round trip')
print(f'Cross-format references: {len(rows)} pixel comparisons and {3*len(rows)} terminal round trips passed')
