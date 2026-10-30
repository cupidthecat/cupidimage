#!/usr/bin/env python3
"""Check embedded image color equations without an external color engine."""
import base64
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from test_pdf_images import fixture
from test_pdf_jpx_containers import box, signature, ihdr, colr, source_values, palette, cmap


def apply(matrix, values):
    return [sum(a * b for a, b in zip(row, values)) for row in matrix]


def inverse(matrix):
    rows = [list(row) + [float(i == j) for j in range(3)] for i, row in enumerate(matrix)]
    for c in range(3):
        pivot = max(range(c, 3), key=lambda r: abs(rows[r][c]))
        rows[c], rows[pivot] = rows[pivot], rows[c]
        divisor = rows[c][c]
        rows[c] = [v / divisor for v in rows[c]]
        for r in range(3):
            if r != c:
                scale = rows[r][c]
                rows[r] = [a - scale * b for a, b in zip(rows[r], rows[c])]
    return [row[3:] for row in rows]


def encode(v):
    v = max(0, min(1, v))
    return round(255 * (12.92 * v if v <= .0031308 else 1.055 * v ** (1 / 2.4) - .055))


def main():
    probe = str(Path(sys.argv[1]).resolve())
    rows = json.loads(Path('tests/fixtures/pdf-jpx-samples.json').read_text())['cases']
    bradford = [[.8951, .2664, -.1614], [-.7502, 1.7135, .0367], [.0389, -.0685, 1.0296]]
    rgb = [[3.2404542, -1.5371385, -.4985314], [-.9692660, 1.8760108, .0415560], [.0556434, -.2040259, 1.0572252]]
    romm = inverse([[1.3460, -.2556, -.0511], [-.5446, 1.5082, .0205], [0, 0, 1.2123]])
    ycc = inverse([[.2990, .5870, .1140], [-.1687, -.3312, .5000], [.5000, -.4187, -.0813]])
    source_white = apply(bradford, [.9642, 1, .8249])
    target_white = apply(bradford, [.95047, 1, 1.08883])
    checks = 0
    with tempfile.TemporaryDirectory(prefix='cupid-jpx-colors-') as directory:
        path = Path(directory) / 'color.pdf'
        for name in ('RGB-unsigned', 'RGB-signed'):
            row = next(r for r in rows if r['name'] == name)
            values = list(zip(*source_values(row)))
            for enumeration in (16, 18, 21):
                payload = signature(True) + box(b'jp2h', ihdr(row) + colr(enumeration, 0, 1)) + box(b'jp2c', base64.b64decode(row['codestream']))
                attrs = f"/Width {row['width']} /Height {row['height']} /Filter /JPXDecode"
                path.write_bytes(fixture(payload, attrs))
                result = subprocess.run([probe, str(path), '0'], capture_output=True, check=True, timeout=30).stdout
                assert struct.unpack('=III', result[:12]) == (24, 16, 1)
                expected = []
                for pixel in values:
                    unit = [(v - (128 if row['signed'] else 0)) / 255 for v in pixel]
                    if enumeration == 16:
                        out = [round(255 * max(0, min(1, v))) for v in unit]
                    elif enumeration == 18:
                        unit = [(pixel[0] - (128 if row['signed'] else 0)) / 255,
                                (pixel[1] - 128) / 255, (pixel[2] - 128) / 255]
                        out = [round(255 * max(0, min(1, v))) for v in apply(ycc, unit)]
                    else:
                        linear = [max(0, min(1, v)) for v in unit]
                        linear = [v / 16 if v < 1 / 32 else v ** 1.8 for v in linear]
                        xyz = apply(romm, linear)
                        cones = apply(bradford, xyz)
                        xyz = apply(inverse(bradford), [v * t / s for v, t, s in zip(cones, target_white, source_white)])
                        out = [encode(v) for v in apply(rgb, xyz)]
                    expected.append(out)
                for y in range(16):
                    for x in range(24):
                        want = expected[(y // 8) * 3 + x // 8]
                        got = result[12 + (y * 24 + x) * 4:][:4]
                        assert got[3] == 255 and all(abs(a - b) <= 1 for a, b in zip(got, want)), (name, enumeration, x, y, got, want)
                checks += 1
    print(f'PDF JPEG 2000 embedded colors: {checks} analytic page checks passed')


if __name__ == '__main__':
    main()
