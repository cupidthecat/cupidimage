#!/usr/bin/env python3
"""Keep small off-axis projection components observable in point movement."""
import ctypes
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import freetype
except ImportError:
    print('TrueType projection precision: skipped (optional freetype-py unavailable)')
    raise SystemExit(0)

from pdf_tt_reference import outline_state
from test_pdf_tt_points_reference import build_font, push


def main():
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(freetype.get_handle(), b'truetype', b'interpreter-version',
                                   ctypes.byref(version)) == 0
    programs = []
    expected = []
    for axis in (0, 1):
        for slope in (-29, 29):
            code = bytes([1 - axis]) + push(0, 768) + b'\x48' + push(1, 775) + b'\x48'
            code += bytes([axis]) + push(0, 0) + b'\x48' + push(1, 704) + b'\x48'
            vector = (554, -1 if slope < 0 else 1) if axis == 0 else (-1 if slope < 0 else 1, 554)
            code += bytes([1 - axis]) + push(*vector) + b'\x0a'
            code += push(0) + b'\x10' + push(1) + b'\x3c'
            programs.append(code)
            expected.append((axis, 769 if slope < 0 else 767))
    with tempfile.TemporaryDirectory(prefix='cupid-tt-projection-') as directory:
        path = Path(directory) / 'projection.ttf'
        build_font(programs, path)
        face = freetype.Face(str(path))
        face.set_pixel_sizes(0, 64)
        for index, (axis, value) in enumerate(expected):
            lines = subprocess.check_output([sys.argv[1], str(path), '--native', '64',
                                             str(index + 1)], text=True).splitlines()
            assert lines[0].startswith('1 0 0 0 '), lines[0]
            native = [tuple(map(int, line.split())) for line in lines[1:]]
            assert native[1][axis] == value and native[1][1 - axis] == 704, native
            face.load_glyph(index + 1, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT |
                            freetype.FT_LOAD_TARGET_MONO)
            actual = outline_state(face)
            assert actual[1][axis] == 768 and actual[1][1 - axis] == 704, actual
            changed = [list(point) for point in actual]
            changed[1][axis] = value
            assert native == list(map(tuple, changed)), (native, actual)
    print(f'TrueType projection precision: 4 analytic cases and 4 recorded axis-shortcut '
          f'differences; engine {freetype.version()}; not counted as independent matches')


if __name__ == '__main__':
    main()
