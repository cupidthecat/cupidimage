#!/usr/bin/env python3
"""Check prepared-state restoration and intentionally persistent twilight points."""
from array import array
import ctypes
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import freetype
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib import newTable
    from fontTools.ttLib.tables.ttProgram import Program
except ImportError:
    print('TrueType state reference: skipped (optional fontTools/freetype-py unavailable)')
    raise SystemExit(0)


def push(*values):
    result = bytearray([0xb8 + len(values) - 1])
    for value in values:
        result.extend((value & 65535).to_bytes(2, 'big'))
    return result


def cases():
    result = []
    read_storage = push(0, 0) + bytes([0x43, 0x48])
    read_cvt = push(0, 0) + bytes([0x45, 0x48])
    for baseline in (-257, -65, 0, 31, 32, 97, 257):
        for changed in (-97, 0, 64, 255):
            prep = push(0, baseline) + bytes([0x42])
            first = push(0, changed) + bytes([0x42]) + read_storage
            result.append((prep, first, read_storage))
            prep = push(0, baseline) + bytes([0x44])
            first = push(0, changed) + bytes([0x44]) + read_cvt
            result.append((prep, first, read_cvt))
    for flags in range(4):
        prep = bytes([0x19]) + push(flags & 1, 1) + bytes([0x8e])
        prep += push(flags & 2, 2) + bytes([0x8e])
        round_zero = push(0, 0) + bytes([0x68, 0x48])
        result.append((prep, round_zero, round_zero))
    for prep_vector in (0x00, 0x02, 0x04):
        for getter in (0x0c, 0x0d):
            for component in (0, 1):
                extract = bytes([0x23, 0x21]) if component else bytes([0x21])
                read = push(0) + bytes([getter]) + extract + bytes([0x01, 0x48])
                result.append((bytes([prep_vector]), bytes([0x00]), read))
                result.append((bytes([prep_vector]), read, read))
    for value in (-257, -65, 0, 31, 32, 97, 257):
        read = push(0, value) + bytes([0x68, 0x48])
        result.append((bytes([0x19]), bytes([0x7a]) + read, read))
        write_twilight = push(0) + bytes([0x16]) + push(1, value) + bytes([0x48])
        read_twilight = push(0, 1) + push(0) + bytes([0x15, 0x46])
        read_twilight += push(1) + bytes([0x15, 0x48])
        result.append((b'', write_twilight, read_twilight))
    # Prep zone selectors do not become glyph defaults.
    result.append((push(0) + bytes([0x16]), push(0, 55) + bytes([0x48]),
                   push(0, 66) + bytes([0x48])))
    # No glyph outline exists during prep. These glyph references have no effect.
    result.append((push(0, 255) + bytes([0x48]), push(0, 0) + bytes([0x46, 0x48]),
                   push(0, 0) + bytes([0x47, 0x48])))
    result.append((push(0, 0) + bytes([0x46, 0x42]), read_storage, read_storage))
    # Prepared definitions remain available in both glyphs.
    prep = push(0) + bytes([0x2c]) + push(42) + bytes([0x2d])
    call = push(0, 0) + bytes([0x2b, 0x48])
    result.append((prep, call, call))
    # The explicit implementation-defaults request has a specification oracle.
    # Some comparison engines retain the prep minimum-distance setting instead.
    minimum = push(192) + bytes([0x1a])
    move = push(1, 0) + bytes([0xec])
    result.append((minimum, move, move))
    result.append((minimum + push(2, 2) + bytes([0x8e]), move, move))
    return result


def build_font(programs, path):
    order = ['.notdef', 'first', 'second']
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
            glyph.program.fromBytecode(programs[i])
        glyphs[name] = glyph
    font = FontBuilder(64, isTTF=True)
    font.setupGlyphOrder(order)
    font.setupCharacterMap({65: 'first', 66: 'second'})
    font.setupGlyf(glyphs)
    font.setupHorizontalMetrics({name: (128, 0) for name in order})
    font.setupHorizontalHeader(ascent=128, descent=0)
    font.setupNameTable({'familyName': 'VM State Test', 'styleName': 'Regular',
                        'uniqueFontIdentifier': 'VM State Test', 'fullName': 'VM State Test',
                        'psName': 'VMStateTest'})
    font.setupOS2(sTypoAscender=128, sTypoDescender=0, usWinAscent=128, usWinDescent=0)
    font.setupPost()
    font.setupMaxp()
    font.font['maxp'].maxStackElements = 64
    font.font['maxp'].maxTwilightPoints = 2
    font.font['maxp'].maxStorage = 2
    font.font['maxp'].maxFunctionDefs = 16
    font.font['cvt '] = newTable('cvt ')
    font.font['cvt '].values = array('h', [64])
    font.font['prep'] = newTable('prep')
    font.font['prep'].program = Program()
    font.font['prep'].program.fromBytecode(programs[0])
    font.save(path)


def main():
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(
        freetype.get_handle(), b'truetype', b'interpreter-version', ctypes.byref(version)
    ) == 0
    programs = cases()
    result = subprocess.run([sys.argv[1], '--state-programs'],
                            input=''.join(':'.join(code.hex() for code in case) + '\n'
                                          for case in programs),
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (result.returncode, result.stderr)
    native = [list(map(int, line.split())) for line in result.stdout.splitlines()]
    assert len(native) == len(programs)
    failures = []
    specified_defaults_differences = []
    with tempfile.TemporaryDirectory(prefix='cupid-tt-state-') as directory:
        for i, case in enumerate(programs):
            path = Path(directory) / f'state-{i}.ttf'
            build_font(case, path)
            face = freetype.Face(str(path))
            face.set_pixel_sizes(1, 1)
            actual = []
            for glyph in (1, 2):
                face.load_glyph(glyph, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT |
                                freetype.FT_LOAD_TARGET_MONO)
                actual.extend(value for point in face.glyph.outline.points for value in point)
            if i >= len(programs) - 2:
                expected_x = 95 if i == len(programs) - 1 else 223
                expected = [31, 35, expected_x, 0, 0, 100] * 2
                assert native[i] == expected, (i, native[i], expected)
            if i == len(programs) - 1 and actual == [31, 35, 223, 0, 0, 100] * 2:
                specified_defaults_differences.append(i)
            elif native[i] != actual:
                failures.append((i, [code.hex() for code in case], native[i], actual))
            del face
    assert not failures, failures[:20]
    print(f'TrueType state reference: {len(programs) - len(specified_defaults_differences)} matches, '
          f'{len(specified_defaults_differences)} specified-defaults difference(s), 0 unexpected failures; '
          f'engine {freetype.version()}; legacy monochrome; supplied geometry only')


if __name__ == '__main__':
    main()
