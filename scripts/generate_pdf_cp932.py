#!/usr/bin/env python3
"""Compile the pinned published CP932 mapping without consulting host codecs."""
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'third_party/unicode-cp932'


def mappings():
    for row in json.loads((SOURCE / 'manifest.json').read_text())['files']:
        assert hashlib.sha256((SOURCE / row['file']).read_bytes()).hexdigest() == row['sha256']
    rows = [(int(a, 16), int(b, 16)) for a, b in re.findall(
        r'^0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s', (SOURCE / 'CP932.TXT').read_text(), re.M)]
    assert rows == sorted(rows) and len(set(a for a, _ in rows)) == len(rows)
    assert all(a <= 65535 and b <= 65535 for a, b in rows)
    return rows


if __name__ == '__main__':
    lines = ['/* Generated from the pinned published CP932 table.',
             ' * Unicode License V3: third_party/unicode-cp932/LICENSE.',
             ' * Copyright 1991-2026 Unicode, Inc. */',
             'static const uint16_t cid_cp932[][2] = {']
    rows = mappings()
    lines += [f'    {{0x{code:04x}, 0x{scalar:04x}}},' for code, scalar in rows]
    data = '\n'.join(lines + ['};', ''])
    path = ROOT / 'src/formats/pdf/fonts/substitute/cp932_data.inc'
    if '--check' in sys.argv:
        assert path.read_text() == data, 'Regenerate CP932 data'
    else:
        path.write_text(data)
    print(f'CP932 mappings: {len(rows)} pinned entries verified')
