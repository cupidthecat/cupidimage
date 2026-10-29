#!/usr/bin/env python3
"""Optional comparisons of authored point programs on one scaled triangle."""
from array import array
import random
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import freetype
    from fontTools.fontBuilder import FontBuilder
    from fontTools.ttLib import newTable
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib.tables.ttProgram import Program
except ImportError:
    print('TrueType point reference: skipped (optional fontTools/freetype-py unavailable)')
    raise SystemExit(0)


def push(*values):
    data = bytearray([0xb8 + len(values) - 1])
    for value in values:
        data.extend((value & 65535).to_bytes(2, 'big'))
    return data


def cases():
    programs = []
    for point in range(3):
        for axis in (0, 1):
            for mode in (0x18, 0x19, 0x3d, 0x7a, 0x7c, 0x7d):
                for value in (-257, -97, -65, -32, -1, 0, 1, 31, 32, 63, 64, 97, 257):
                    code = bytes([axis, mode]) + push(point, value) + bytes([0x48])
                    programs.append(code + push(point) + bytes([0x2f]))
    for op in (0x06, 0x07, 0x08, 0x09, 0x86, 0x87):
        for a in range(3):
            for b in range(3):
                code = push(a, b) + bytes([op])
                # Read the selected vector into glyph coordinates after resetting
                # both axes, so the probe movement cannot distort that value.
                getter = 0x0d if op in (0x08, 0x09) else 0x0c
                for component in (0, 1):
                    extract = bytes([0x23, 0x21]) if component else bytes([0x21])
                    programs.append(push(0) + code + bytes([getter]) + extract + bytes([0x01, 0x48]))
    for op in (0x06, 0x07, 0x86, 0x87):
        for a, b in ((0, 1), (1, 0), (1, 2), (2, 1)):
            # Exercise movement with diagonal projection and freedom vectors.
            code = push(1, 210) + bytes([0x48]) + push(a, b) + bytes([op, 0x0e])
            programs.append(code + push(2, 73) + bytes([0x48]))
            # Original coordinates are measured through the dual vector.
            programs.append(code + push(0, 2) + bytes([0x47, 0x01, 0x48]))
    for axis in (0, 1):
        for value in (-257, -1, 0, 1, 97, 257):
            for getter in (0x46, 0x47):
                code = bytes([axis]) + push(0) + bytes([0x16])
                code += push(1, value) + bytes([0x48])
                code += push(0, 1) + bytes([getter]) + push(1) + bytes([0x15, 0x48])
                programs.append(code)
    rng = random.Random(571)
    vectors = [(0, 0), (1, 1), (-32768, 32767), (3, 5)]
    vectors += [(rng.randrange(-32768, 32768), rng.randrange(-32768, 32768)) for _ in range(500)]
    for x, y in vectors:
        for setter, getter in ((0x0a, 0x0c), (0x0b, 0x0d)):
            for component in (0, 1):
                extract = bytes([0x23, 0x21]) if component else bytes([0x21])
                programs.append(push(0) + push(x, y) + bytes([setter, getter]) +
                                extract + bytes([0x01, 0x48]))
    # Coincident original twilight points with distinct current positions.
    coincident = push(0) + bytes([0x16]) + push(0, 31) + bytes([0x48])
    coincident += push(1, 31) + bytes([0x48]) + push(0) + bytes([0x2f])
    coincident += push(0, 1) + bytes([0x87])
    for component in (0, 1):
        extract = bytes([0x23, 0x21]) if component else bytes([0x21])
        programs.append(coincident + push(0) + bytes([0x0c]) + extract +
                        bytes([0x01]) + push(1) + bytes([0x15, 0x48]))
    programs.append(coincident + push(0, 0) + bytes([0x47, 0x01]) +
                    push(1) + bytes([0x15, 0x48]))
    # Coincident current points must not erase the distinct original direction.
    coincident = push(1, 31) + bytes([0x48, 0x00]) + push(1, 35) + bytes([0x48, 0x01])
    coincident += push(0, 1) + bytes([0x87])
    for component in (0, 1):
        extract = bytes([0x23, 0x21]) if component else bytes([0x21])
        programs.append(coincident + push(0) + bytes([0x0c]) + extract + bytes([0x01, 0x48]))
    programs.append(coincident + push(0, 2) + bytes([0x47, 0x01, 0x48]))
    # MIAP cut-in boundaries, both coordinate axes and all predefined rounds.
    for axis in (0, 1):
        for rounding in (0x18, 0x19, 0x3d, 0x7a, 0x7c, 0x7d):
            for target in (-257, -38, -37, -1, 0, 31, 32, 63, 64, 99, 100, 257):
                for cutin in (-1, 0, 16, 68, 257):
                    for op in (0x3e, 0x3f):
                        code = bytes([axis, rounding]) + push(0, target) + bytes([0x44])
                        code += push(cutin) + bytes([0x1d]) + push(0, 0) + bytes([op])
                        programs.append(code)
    for op in (0x3e, 0x3f):
        for target in (-257, -37, 0, 31, 32, 97, 257):
            for getter in (0x46, 0x47):
                code = push(0, target) + bytes([0x44]) + push(0) + bytes([0x16])
                code += push(3, 5) + bytes([0x0a, 0x0e]) + push(1, 0) + bytes([op])
                code += push(0, 1) + bytes([getter, 0x01]) + push(1) + bytes([0x15, 0x48])
                programs.append(code)
    for band, op in enumerate((0x5d, 0x71, 0x72)):
        for shift in range(7):
            for argument in range(256):
                base = 64 - band * 16 - (argument >> 4)
                for matching in (True, False):
                    code = push(base if matching else base + 1) + bytes([0x5e])
                    code += push(shift) + bytes([0x5f]) + push(argument, 1, 1) + bytes([op])
                    programs.append(code)
        # DELTA movement uses projection, unlike SHPIX's direct freedom movement.
        code = push(64 - band * 16) + bytes([0x5e]) + push(3, 5) + bytes([0x0b])
        programs.append(code + push(8, 0, 1) + bytes([op]))
        for getter in (0x46, 0x47):
            code = push(64 - band * 16) + bytes([0x5e]) + push(0) + bytes([0x16])
            code += push(8, 1, 1) + bytes([op]) + push(0, 1) + bytes([getter])
            code += push(1) + bytes([0x15, 0x48])
            programs.append(code)
    for freedom in ((16384, 0), (0, 16384), (9830, 13107), (-9830, 13107)):
        for distance in (-257, -97, -64, -1, 0, 1, 31, 64, 97, 257):
            for count in (0, 1, 3):
                code = push(*freedom) + bytes([0x0b]) + push(count) + bytes([0x17])
                code += push(*tuple(range(count)), distance) + bytes([0x38])
                programs.append(code)
    for op in (0x32, 0x33):
        setter = 0x11 if op & 1 else 0x12
        for axis in (0, 1):
            for distance in (-257, -65, -1, 0, 1, 31, 64, 257):
                for indices in ((0,), (1,), (2,), (0, 1, 2), (1, 1, 0)):
                    code = bytes([axis]) + push(1, distance) + bytes([0x48])
                    code += push(1) + bytes([setter]) + push(len(indices)) + bytes([0x17])
                    code += push(*indices) + bytes([op])
                    programs.append(code)
        code = push(0) + bytes([0x16]) + push(1, 31) + bytes([0x48])
        code += push(1) + bytes([0x2f]) + push(1) + bytes([setter])
        code += push(1) + bytes([0x15]) + push(1) + bytes([0x14 if op & 1 else 0x13])
        code += push(0) + bytes([op])
        programs.append(code)
        programs.append(push(0) + bytes([0x17, op]))
        for projection in ((16384, 0), (3, 4)):
            for freedom in ((3, 5), (-3, 5)):
                code = push(0, 97) + bytes([0x48]) + push(0) + bytes([setter])
                code += push(*projection) + bytes([0x0a]) + push(*freedom) + bytes([0x0b])
                programs.append(code + push(2) + bytes([op]))
    # The reference face can retain twilight storage between glyph loads.
    # Give both engines the same explicit initial coordinates for each program.
    reset = push(0) + bytes([0x16, 0x01])
    reset += push(0, 0) + bytes([0x48]) + push(1, 0) + bytes([0x48, 0x00])
    reset += push(0, 0) + bytes([0x48]) + push(1, 0) + bytes([0x48])
    reset += push(1) + bytes([0x16, 0x01])
    return [reset + code for code in programs]


def build_font(programs, path):
    order = ['.notdef'] + [f'g{i}' for i in range(len(programs))]
    glyphs = {}
    for i, name in enumerate(order):
        pen = TTGlyphPen(None)
        pen.moveTo((31, 35))
        pen.lineTo((100, 0))
        pen.lineTo((0, 100))
        pen.closePath()
        glyph = pen.glyph()
        if i:
            glyph.program = Program()
            glyph.program.fromBytecode(programs[i - 1])
        glyphs[name] = glyph
    # At 64 ppem, 4096 units per em gives an exact design-unit-to-F26Dot6 mapping.
    font = FontBuilder(4096, isTTF=True)
    font.setupGlyphOrder(order)
    font.setupCharacterMap({65: 'g0'})
    font.setupGlyf(glyphs)
    font.setupHorizontalMetrics({name: (128, 0) for name in order})
    font.setupHorizontalHeader(ascent=128, descent=0)
    font.setupNameTable({'familyName': 'VM Point Test', 'styleName': 'Regular',
                        'uniqueFontIdentifier': 'VM Point Test', 'fullName': 'VM Point Test',
                        'psName': 'VMPointTest'})
    font.setupOS2(sTypoAscender=128, sTypoDescender=0, usWinAscent=128, usWinDescent=0)
    font.setupPost()
    font.setupMaxp()
    font.font['maxp'].maxStackElements = 64
    font.font['maxp'].maxTwilightPoints = 2
    font.font['cvt '] = newTable('cvt ')
    font.font['cvt '].values = array('h', [64, 128, -64, 0])
    font.save(path)


def main():
    programs = cases()
    result = subprocess.run([sys.argv[1], '--point-programs'],
                            input=''.join(code.hex() + '\n' for code in programs),
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (result.returncode, result.stderr)
    native = [list(map(int, line.split())) for line in result.stdout.splitlines()]
    assert len(native) == len(programs)
    failures = []
    with tempfile.TemporaryDirectory(prefix='cupid-tt-points-') as directory:
        path = Path(directory) / 'points.ttf'
        build_font(programs, path)
        face = freetype.Face(str(path))
        face.set_pixel_sizes(64, 64)
        for i, code in enumerate(programs):
            face.load_glyph(i + 1, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT |
                            freetype.FT_LOAD_TARGET_MONO)
            actual = [value for point in face.glyph.outline.points for value in point]
            if native[i] != actual:
                failures.append((i, code.hex(), native[i], actual))
        del face
    assert not failures, failures[:20]
    print(f'TrueType point reference: {len(programs)} programs, 0 failures; '
          f'engine {freetype.version()}; page renderer not involved')


if __name__ == '__main__':
    main()
