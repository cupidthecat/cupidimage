#!/usr/bin/env python3
"""Check initial CVT scaling with non-power-of-two font units."""
from array import array
import ctypes
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import freetype
    from fontTools.ttLib import TTFont
except ImportError:
    print('TrueType CVT precision: skipped (optional fontTools/freetype-py unavailable)')
    raise SystemExit(0)

from test_pdf_tt_points_reference import build_font, push


def nearest(numerator, denominator):
    value = (abs(numerator) + denominator // 2) // denominator
    return -value if numerator < 0 else value


def main():
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(freetype.get_handle(), b'truetype', b'interpreter-version',
                                   ctypes.byref(version)) == 0
    values = (-32768, -1886, -727, -65, -1, 0, 1, 65, 727, 1886, 32767)
    programs = [b'\x01' + push(0, i) + b'\x45\x48' for i in range(len(values))]
    matched = differences = 0
    with tempfile.TemporaryDirectory(prefix='cupid-tt-cvt-') as directory:
        path = Path(directory) / 'scaling.ttf'
        build_font(programs, path)
        with TTFont(path) as font:
            font['head'].unitsPerEm = 1000
            font['cvt '].values = array('h', values)
            font.save(path)
        face = freetype.Face(str(path))
        for size in (1, 6, 9, 13, 16, 24, 37, 64, 96):
            scale = (size * 64 * 65536 + 500) // 1000
            expected = [nearest(value * scale, 65536) for value in values]
            result = subprocess.check_output([sys.argv[1], '--prep-cvt', str(path), str(size)], text=True)
            status = list(map(int, result.split()))
            assert status[0] == 1 and status[2:5] == [0, 0, 0], status
            assert status[8:] == expected, (size, status[8:], expected)
            face.set_pixel_sizes(0, size)
            for i, value in enumerate(expected):
                face.load_glyph(i + 1, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT |
                                freetype.FT_LOAD_TARGET_MONO)
                actual = face.glyph.outline.points[0][0]
                if actual == value:
                    matched += 1
                else:
                    # The older engine discards six bits from the scale before
                    # multiplying CVT values represented with six extra bits.
                    truncated = nearest(values[i] * 64 * (scale // 64), 65536)
                    assert freetype.version() == (2, 13, 2) and actual == truncated, (size, i, actual, value)
                    differences += 1
    assert matched + differences == 99
    print(f'TrueType CVT precision: 99 analytic values, {matched} independent matches, '
          f'{differences} recorded older-engine scaling differences; engine {freetype.version()}')


if __name__ == '__main__':
    main()
