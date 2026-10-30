#!/usr/bin/env python3
"""Compare retained Type 1 private fields with an independent font parser."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_pdf_cff_hints import FIELDS, DEFAULTS


def pfb_program(data):
    """Strip PFB framing for the PDF FontFile interface, preserving binary bytes."""
    pos = 0
    clear, encrypted = bytearray(), bytearray()
    binary = False
    while pos + 2 <= len(data):
        if data[pos] != 128:
            raise ValueError('Invalid PFB segment marker')
        kind = data[pos + 1]
        pos += 2
        if kind == 3:
            if pos != len(data) or not encrypted:
                raise ValueError('Invalid PFB end marker')
            return bytes(clear + encrypted), len(clear), len(encrypted)
        if kind not in (1, 2) or pos + 4 > len(data):
            raise ValueError('Invalid PFB segment type')
        size = int.from_bytes(data[pos:pos + 4], 'little')
        pos += 4
        if size > len(data) - pos:
            raise ValueError('Truncated PFB segment')
        chunk = data[pos:pos + size]
        pos += size
        if kind == 2:
            binary = True
            encrypted.extend(chunk)
        elif not binary:
            clear.extend(chunk)
    raise ValueError('Missing PFB end marker')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    parser.add_argument('fonts', type=Path, nargs='+', help='PFB files or directories containing them')
    args = parser.parse_args()
    from fontTools.t1Lib import T1Font
    paths = sorted({p for item in args.fonts for p in
                    (item.glob('*.pfb') if item.is_dir() else [item])})
    if not paths:
        parser.error('No PFB fonts selected')
    fields = 0
    unresolved = []
    with tempfile.TemporaryDirectory(prefix='cupid-type1-private-reference-') as tmp:
        path = Path(tmp) / 'font.bin'
        for font in paths:
            data, clear, encrypted = pfb_program(font.read_bytes())
            path.write_bytes(data)
            result = subprocess.run([str(args.probe.resolve()), str(path), str(clear),
                                     str(encrypted), '--hints'], capture_output=True, text=True,
                                    check=True, timeout=30)
            values = {}
            masks = None
            for line in result.stdout.splitlines():
                tokens = line.split()
                if tokens[0] == 'hints':
                    masks = tuple(map(int, tokens[1:]))
                elif tokens[0] == 'hint-array':
                    values[int(tokens[1])] = list(map(float, tokens[2:]))
                elif tokens[0] == 'hint-scalar':
                    values[int(tokens[1])] = float(tokens[2])
            independent = T1Font(str(font))
            independent.parse()
            private = independent.font['Private']
            assert masks is not None
            assert masks[1] == masks[2], (font, 'unexpected invalid literal hints', masks)
            for i, key in enumerate(FIELDS):
                if masks[2] & (1 << i):
                    unresolved.append((font.name, key))
                    continue
                expected = private.get(key, DEFAULTS[i])
                if key in ('StdHW', 'StdVW') and isinstance(expected, list):
                    expected = expected[0] if expected else 0
                assert values[i] == expected, (font, key, values[i], expected)
                assert bool(masks[0] & (1 << i)) == (key in private), (font, key, masks)
                fields += 1
    print(f'Type 1 private reference: {fields} matching fields in {len(paths)} fonts; '
          f'{len(unresolved)} unresolved expressions; no grid fitting')
    for name, key in unresolved:
        print(f'Unresolved: {name}: {key}')


if __name__ == '__main__':
    main()
