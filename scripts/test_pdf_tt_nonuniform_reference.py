#!/usr/bin/env python3
"""Compare native loading and projection-dependent sizing on authored glyphs."""
import ctypes
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import freetype
except ImportError:
    print('TrueType nonuniform reference: skipped (optional freetype-py unavailable)')
    raise SystemExit(0)

from pdf_tt_reference import outline_state
from test_pdf_tt_points_reference import build_font, push


def programs():
    result = []
    for axis in (0, 1):
        vector = bytes([axis])
        for index in range(4):
            result.append(vector + push(0, index) + b'\x45\x48')
            result.append(vector + push(1, index) + b'\x3e')
            result.append(vector + push(1, index) + b'\x3f')
            result.append(vector + push(1, index) + b'\xe4')
        for value in (-4096, -97, -1, 0, 1, 97, 4096):
            for op in (0x44, 0x70):
                for read_axis in (0, 1):
                    code = vector + push(1, value) + bytes([op, read_axis])
                    result.append(code + push(0, 1) + b'\x45\x48')
        for band, op in enumerate((0x73, 0x74, 0x75)):
            for relative in range(16):
                argument = (relative << 4) | 8
                result.append(vector + push(argument, 3, 1) + bytes([op]) + push(0, 3) + b'\x45\x48')
                point_op = (0x5d, 0x71, 0x72)[band]
                result.append(vector + push(argument, 1, 1) + bytes([point_op]))
        for op in (0xc0, 0xc4, 0xcc):
            result.append(vector + push(1) + bytes([op]))
        code = vector + push(0, 64) + b'\x48' + push(1, 97) + b'\x48'
        result.append(code + push(0) + b'\x11' + push(1) + b'\x12' + push(2) + b'\x39')
        result.append(code + bytes([0x31 if axis else 0x30]))
    for x, y in ((3, 4), (4, 3), (-3, 4), (13, -7)):
        vector = push(x, y) + b'\x0a\x0e'
        result.append(vector + push(0) + b'\x4b\x01\x48')
        for index in range(4):
            result.append(vector + push(0, index) + b'\x45\x01\x48')
            result.append(vector + push(1, index) + b'\x3e')
        for op in (0xc0, 0xc4, 0xcc):
            result.append(vector + push(1) + bytes([op]))
    result += [push(0, 42, 97) + bytes([op, 0x48]) for op in (0x7e, 0x7f)]
    result += [push(0) + b'\x4c\x01\x48', push(0, 255) + b'\x88\x01\x48']
    return result


def main():
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(freetype.get_handle(), b'truetype', b'interpreter-version',
                                   ctypes.byref(version)) == 0
    cases = programs()
    checked = mps_differences = 0
    with tempfile.TemporaryDirectory(prefix='cupid-tt-nonuniform-') as directory:
        path = Path(directory) / 'nonuniform.ttf'
        build_font(cases, path)
        for gray in (False, True):
            for x, y in ((9, 16), (16, 9), (13, 37), (37, 13), (12, 24), (24, 12)):
                for i, code in enumerate(cases):
                    lines = subprocess.check_output([sys.argv[1], str(path),
                        '--native-xy-gray' if gray else '--native-xy', str(x), str(y), str(i+1)], text=True).splitlines()
                    assert lines[0].startswith('1 0 0 0 '), (x, y, i, code.hex(), lines[0])
                    native = [tuple(map(int, line.split())) for line in lines[1:]]
                    target = freetype.FT_LOAD_TARGET_NORMAL if gray else freetype.FT_LOAD_TARGET_MONO
                    # Match the native probe's fresh preparation for every case.
                    # Paired-glyph state is qualified by separate lifetime tests.
                    face = freetype.Face(str(path))
                    face.set_pixel_sizes(x, y)
                    face.load_glyph(i+1, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT | target)
                    actual = outline_state(face)
                    if i == len(cases) - 2 and native != actual:
                        # MPS specifies F26Dot6 point size. This legacy comparison
                        # mode returns integer projected ppem instead.
                        assert actual[0][0] == x and native[0][0] == y * 64
                        expected = [list(point) for point in actual]
                        expected[0][0] = y * 64
                        assert native == list(map(tuple, expected)), (x, y, native, actual)
                        mps_differences += 1
                    else:
                        assert native == actual, (x, y, gray, i, code.hex(), native, actual)
                        checked += 1
                    del face
    print(f'TrueType nonuniform reference: {len(cases)} programs, {checked} complete outline matches; '
          f'{mps_differences} specified MPS differences; 6 axis-size pairs, mono/grayscale; engine {freetype.version()}; no PDF rendering')


if __name__ == '__main__':
    main()
