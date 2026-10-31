#!/usr/bin/env python3
"""Compile pinned published code-page mappings without consulting host codecs."""
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'third_party/unicode-codepages'


def mappings(codepage=932):
    assert codepage in (932, 936, 949, 950)
    for row in json.loads((SOURCE / 'manifest.json').read_text())['files']:
        assert hashlib.sha256((SOURCE / row['file']).read_bytes()).hexdigest() == row['sha256']
    rows = [(int(a, 16), int(b, 16)) for a, b in re.findall(
        r'^0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s', (SOURCE / f'CP{codepage}.TXT').read_text(), re.M)]
    assert rows == sorted(rows) and len(set(a for a, _ in rows)) == len(rows)
    assert all(a <= 65535 and b <= 65535 for a, b in rows)
    return rows


def main(codepages=(932, 936, 949, 950)):
    for codepage in codepages:
        lines = [f'/* Generated from the pinned published CP{codepage} table.',
                 ' * Unicode License V3: third_party/unicode-codepages/LICENSE.',
                 ' * Copyright 1991-2026 Unicode, Inc. */',
                 f'static const uint16_t cid_cp{codepage}[][2] = {{']
        rows = mappings(codepage)
        lines += [f'    {{0x{code:04x}, 0x{scalar:04x}}},' for code, scalar in rows]
        data = '\n'.join(lines + ['};', ''])
        path = ROOT / f'src/formats/pdf/fonts/substitute/cp{codepage}_data.inc'
        if '--check' in sys.argv:
            assert path.read_text() == data, f'Regenerate CP{codepage} data'
        else:
            path.write_text(data)
        print(f'CP{codepage} mappings: {len(rows)} pinned entries verified')


if __name__ == '__main__':
    main()
