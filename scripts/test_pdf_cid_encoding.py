#!/usr/bin/env python3
"""Check every pinned CP932 mapping and Unicode scalar against an independent oracle."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib
from generate_pdf_cp932 import mappings


def main():
    probe = str(Path(sys.argv[1]).resolve())
    reference = json.loads(Path('tests/fixtures/pdf-cid-substitute-mapping.json').read_text())
    rows = list(struct.iter_unpack('>I4H', zlib.decompress(base64.b64decode(reference['records_zlib']))))
    assert len(rows) == reference['count']
    scalars = {row[0]: row[1:] for row in rows}
    assert len(scalars) == len(rows)
    cp932 = mappings()
    checks = 0
    for index, (face, record) in enumerate(zip(('gothic', 'mincho'), reference['fonts'])):
        path = Path('third_party/cid-standard-fonts') / record['file']
        assert hashlib.sha256(path.read_bytes()).hexdigest() == record['sha256']
        for encoding in ('cp932', 'ucs2', 'utf16'):
            cases = []
            if encoding == 'cp932':
                cases = [(code, 1 if code < 256 else 2, scalar) for code, scalar in cp932]
                cases += [(0x81, 1, None), (0xa0, 1, None), (0x41, 2, None),
                          (0x8140, 1, None), (0xffff, 2, None)]
            else:
                for scalar in scalars:
                    if scalar <= 65535:
                        cases.append((scalar, 2, scalar))
                    elif encoding == 'utf16':
                        value = scalar - 0x10000
                        code = ((0xd800 + (value >> 10)) << 16) | (0xdc00 + (value & 1023))
                        cases.append((code, 4, scalar))
                cases += [(0xd800, 2, None), (0xdc00, 2, None), (0x41, 1, None),
                          (0xd8000041, 4, None), (0x0041dc00, 4, None), (0x110000, 2, None)]
            inputs = ''.join(f'{code:08x} {length}\n' for code, length, _ in cases)
            result = subprocess.run([probe, str(path), face, encoding], input=inputs,
                                    capture_output=True, text=True, check=True, timeout=60)
            actual = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
            expected = [tuple(scalars.get(scalar, (0, 0, 0, 0))[index * 2:index * 2 + 2])
                        for _, _, scalar in cases]
            assert actual == expected, (face, encoding, next(
                ((case, a, b) for case, a, b in zip(cases, actual, expected) if a != b), None))
            checks += len(cases) * 2
    print(f'CID Unicode/vertical selection: {checks} independent glyph-index checks passed')


if __name__ == '__main__':
    main()
