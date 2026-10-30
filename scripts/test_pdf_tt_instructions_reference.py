#!/usr/bin/env python3
"""Compare authored instruction definitions and exact line intersections."""
import ctypes
import itertools
import subprocess
import sys
import tempfile
from fractions import Fraction
from pathlib import Path

try:
    import freetype
    from fontTools.ttLib import TTFont, newTable
    from fontTools.ttLib.tables.ttProgram import Program
except ImportError:
    print('TrueType instruction reference: skipped (optional fontTools/freetype-py unavailable)')
    raise SystemExit(0)

from pdf_tt_reference import outline_state
from test_pdf_tt_points_reference import build_font, push


def rounded(value):
    value = Fraction(value)
    magnitude = (2 * abs(value.numerator) + value.denominator) // (2 * value.denominator)
    return -magnitude if value < 0 else magnitude


def reference_intersection(a, b, c, d):
    da = [b[i] - a[i] for i in (0, 1)]
    db = [d[i] - c[i] for i in (0, 1)]
    offset = [c[i] - a[i] for i in (0, 1)]
    cross = da[0] * db[1] - da[1] * db[0]
    if not cross:
        return [int(Fraction(a[i] + b[i] + c[i] + d[i], 4)) for i in (0, 1)]
    numerator = offset[0] * db[1] - offset[1] * db[0]
    return [a[i] + rounded(Fraction(numerator * da[i], cross)) for i in (0, 1)]


def main():
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(freetype.get_handle(), b'truetype', b'interpreter-version',
                                   ctypes.byref(version)) == 0
    matched = differences = definitions = 0
    with tempfile.TemporaryDirectory(prefix='cupid-tt-instructions-') as directory:
        path = Path(directory) / 'intersections.ttf'
        cases = list(itertools.product(range(3), repeat=5))
        programs = [push(*case) + b'\x0f' for case in cases]
        build_font(programs, path)
        face = freetype.Face(str(path))
        for size in (13, 37, 64):
            face.set_pixel_sizes(0, size)
            points = [[rounded(Fraction(x * size, 64)), rounded(Fraction(y * size, 64)), 1, int(i == 2)]
                      for i, (x, y) in enumerate(((31, 35), (100, 0), (0, 100)))]
            for index, (target, a, b, c, d) in enumerate(cases):
                expected = [list(point) for point in points]
                expected[target][:2] = reference_intersection(points[a], points[b], points[c], points[d])
                lines = subprocess.check_output([sys.argv[1], str(path), '--native', str(size),
                                                 str(index + 1)], text=True).splitlines()
                assert lines[0].startswith('1 0 0 0 '), lines[0]
                native = [tuple(map(int, line.split())) for line in lines[1:]]
                assert native == list(map(tuple, expected)), (size, index, native, expected)
                face.load_glyph(index + 1, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT |
                                freetype.FT_LOAD_TARGET_MONO)
                actual = outline_state(face)
                if native == actual:
                    matched += 1
                else:
                    # The independent engine quantizes each determinant product
                    # before division, even when two lines share an endpoint.
                    pa, pb, pc, pd = (points[i] for i in (a, b, c, d))
                    da = [pb[i] - pa[i] for i in (0, 1)]
                    db = [pd[i] - pc[i] for i in (0, 1)]
                    offset = [pc[i] - pa[i] for i in (0, 1)]
                    determinant = rounded(Fraction(-da[0] * db[1], 64)) + rounded(Fraction(da[1] * db[0], 64))
                    numerator = rounded(Fraction(-offset[0] * db[1], 64)) + rounded(Fraction(offset[1] * db[0], 64))
                    quantized = [list(point) for point in points]
                    quantized[target][:2] = [pa[i] + rounded(Fraction(numerator * da[i], determinant))
                                            for i in (0, 1)]
                    assert actual == list(map(tuple, quantized)), (size, index, native, actual)
                    differences += 1
        del face
        assert (matched, differences) == (717, 12), (matched, differences)
        opcodes = [0x28, 0x7b, 0x83, 0x84, *range(0x8f, 0x91), *range(0x93, 0xb0)]
        path = Path(directory) / 'definitions.ttf'
        programs = [push(0) + bytes([opcode, 0x48]) for opcode in opcodes]
        build_font(programs, path)
        with TTFont(path) as font:
            font['maxp'].maxInstructionDefs = len(opcodes)
            for name in ('fpgm', 'prep'):
                table = newTable(name)
                table.program = Program()
                table.program.fromBytecode(b''.join(push(opcode) + b'\x89' + push(i + (70 if name == 'prep' else 10)) + b'\x2d'
                                                    for i, opcode in enumerate(opcodes)))
                font[name] = table
            font.save(path)
        face = freetype.Face(str(path))
        for size in (13, 37, 64):
            face.set_pixel_sizes(0, size)
            for index in range(len(programs)):
                lines = subprocess.check_output([sys.argv[1], str(path), '--native', str(size),
                                                 str(index + 1)], text=True).splitlines()
                assert lines[0].startswith('1 0 0 0 '), lines[0]
                native = [tuple(map(int, line.split())) for line in lines[1:]]
                assert native[0][0] == index + 70, (index, native)
                face.load_glyph(index + 1, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT |
                                freetype.FT_LOAD_TARGET_MONO)
                assert native == outline_state(face), (index, native, outline_state(face))
                definitions += 1
    print(f'TrueType instruction reference: {definitions} definition matches; '
          f'{matched} intersection matches, {differences} determinant-quantization differences; '
          f'all 729 intersections match exact rational expectations; engine {freetype.version()}')


if __name__ == '__main__':
    main()
