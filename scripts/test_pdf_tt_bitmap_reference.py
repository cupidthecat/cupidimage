#!/usr/bin/env python3
"""Compare the native instruction/transform/coverage pipeline on authored fonts."""
import ctypes
import itertools
import subprocess
import sys
import tempfile
from pathlib import Path

from test_pdf_coverage_reference import freetype, native_coverage, reference_coverage
from pdf_tt_reference import outline_state
from test_pdf_tt_points_reference import build_font, push
from test_pdf_tt_composite_transforms import build

version = ctypes.c_uint(35)
assert freetype.FT_Property_Set(freetype.get_handle(), b'truetype', b'interpreter-version',
                               ctypes.byref(version)) == 0
matrices = ((65536, 0, 0, 65536), (-65536, 0, 0, 65536), (0, 65536, -65536, 0),
            (46341, 46341, -46341, 46341), (98304, 32768, -16384, 49152),
            (32768, 0, 0, 98304))
offsets = ((0, 0), (17, -23), (-129, 97), (1024, -2048))
sizes = ((13, 13), (13, 37), (37, 13))
checked = 0
with tempfile.TemporaryDirectory(prefix='cupid-tt-bitmap-') as directory:
    path = Path(directory) / 'bitmap.ttf'
    for composite in (False, True):
        if composite:
            build(path, (.75, .25, -.125, 1), (-50, 73), push(1) + b'\x2f',
                  b'\x00' + push(0) + b'\x2f', 2)
            glyph = 3
        else:
            build_font([push(1) + b'\x2f\x00' + push(0) + b'\x2f'], path)
            glyph = 1
        for (x, y), matrix, offset in itertools.product(sizes, matrices, offsets):
            lines = subprocess.check_output([sys.argv[1], str(path), '--native-matrix-gray',
                str(x), str(y), str(glyph), *map(str, matrix), *map(str, offset)], text=True).splitlines()
            assert lines[0].startswith('1 0 0 0 '), lines[0]
            native = [tuple(map(int, line.split())) for line in lines[1:]]
            face = freetype.Face(str(path))
            face.set_pixel_sizes(x, y)
            face.set_transform(freetype.Matrix(matrix[0], matrix[2], matrix[1], matrix[3]),
                               freetype.Vector(*offset))
            face.load_glyph(glyph, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT)
            reference = outline_state(face)
            assert native == reference
            left = min(p[0] for p in native) // 64 - 1
            bottom = min(p[1] for p in native) // 64 - 1
            right = (max(p[0] for p in native) + 63) // 64 + 1
            top = (max(p[1] for p in native) + 63) // 64 + 1
            def shifted(points):
                return [(px - left * 64, py - bottom * 64, on, end)
                        for px, py, on, end in points]
            actual = native_coverage(sys.argv[2], shifted(native), right - left, top - bottom)
            expected = reference_coverage(shifted(reference), right - left, top - bottom)
            assert actual == expected, (composite, x, y, matrix, offset)
            checked += 1
print(f'TrueType bitmap pipeline: {checked} exact grayscale matches; simple/composite, '
      f'nonuniform sizes and affine transforms; engine {freetype.version()}; no PDF pages')
