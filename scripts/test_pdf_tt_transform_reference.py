#!/usr/bin/env python3
"""Compare post-instruction affine outlines without rasterizing PDF pages."""
import ctypes
import itertools
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import freetype
except ImportError:
    print('TrueType transformed reference: skipped (optional freetype-py unavailable)')
    raise SystemExit(0)

from pdf_tt_reference import outline_state
from test_pdf_tt_points_reference import build_font, push
from test_pdf_tt_composite_transforms import build


def main():
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(freetype.get_handle(), b'truetype', b'interpreter-version',
                                   ctypes.byref(version)) == 0
    matrices = ((65536,0,0,65536), (-65536,0,0,65536), (0,65536,-65536,0),
                (46341,46341,-46341,46341), (98304,32768,-16384,49152),
                (1,0,0,1), (0,0,0,0), (65536,65536,65536,65536), (32768,0,0,98304))
    offsets = ((0,0), (17,-23), (-129,97), (1024,-2048))
    sizes = ((13,13), (13,37), (37,13))
    checked = 0
    with tempfile.TemporaryDirectory(prefix='cupid-tt-transform-') as directory:
        path = Path(directory) / 'transform.ttf'
        for composite in (False, True):
            if composite:
                build(path, (.75,.25,-.125,1), (-50,73), push(1) + b'\x2f', b'\x00' + push(0) + b'\x2f', 2)
                index = 3
            else:
                build_font([push(1) + b'\x2f\x00' + push(0) + b'\x2f'], path)
                index = 1
            for gray, (x,y), matrix, offset in itertools.product((False,True), sizes, matrices, offsets):
                result = subprocess.check_output([sys.argv[1], str(path),
                    '--native-matrix-gray' if gray else '--native-matrix', str(x), str(y), str(index),
                    *map(str, matrix), *map(str, offset)], text=True).splitlines()
                assert result[0].startswith('1 0 0 0 '), (composite, x, y, matrix, result[0])
                native = [tuple(map(int, line.split())) for line in result[1:]]
                face = freetype.Face(str(path))
                face.set_pixel_sizes(x, y)
                face.set_transform(freetype.Matrix(matrix[0],matrix[2],matrix[1],matrix[3]), freetype.Vector(*offset))
                face.load_glyph(index, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT |
                                (freetype.FT_LOAD_TARGET_NORMAL if gray else freetype.FT_LOAD_TARGET_MONO))
                assert native == outline_state(face), (composite, gray, x, y, matrix, offset, native, outline_state(face))
                checked += 1
                del face
    print(f'TrueType transformed reference: {checked} complete outline matches; simple/composite, '
          f'rotations/reflections/shears/translations/degeneracy; engine {freetype.version()}; no PDF rendering')


if __name__ == '__main__':
    main()
