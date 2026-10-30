#!/usr/bin/env python3
"""Probe authored composite transforms, nesting and parent/child instructions."""
import ctypes
import copy
from array import array
import itertools
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import freetype
    from fontTools.fontBuilder import FontBuilder
    from fontTools.ttLib import TTFont, newTable
    from fontTools.ttLib.tables._g_l_y_f import Glyph, GlyphComponent
    from fontTools.ttLib.tables.ttProgram import Program
    from fontTools.pens.ttGlyphPen import TTGlyphPen
except ImportError:
    print('TrueType composite transforms: skipped (optional fontTools/freetype-py unavailable)')
    raise SystemExit(0)

from pdf_tt_reference import outline_state


def compound(name, matrix, offset, flags, code=b''):
    component = GlyphComponent()
    component.glyphName = name
    component.x, component.y = offset
    component.flags = flags
    component.transform = [list(matrix[:2]), list(matrix[2:])]
    glyph = Glyph()
    glyph.numberOfContours = -1
    glyph.components = [component]
    if code:
        glyph.program = Program()
        glyph.program.fromBytecode(code)
    return glyph


def build(path, matrix, offset, child_code, parent_code, index, scaled_offset=False):
    pen = TTGlyphPen(None)
    pen.moveTo((31, 35))
    pen.lineTo((100, 0))
    pen.lineTo((0, 100))
    pen.closePath()
    base = pen.glyph()
    base.program = Program()
    base.program.fromBytecode(child_code)
    flags = (0x800 if scaled_offset else 0x1000) | (4 if index & 1 else 0) | (512 if index % 5 == 0 else 0)
    part = compound('base', matrix, offset, flags, parent_code)
    root = compound('part', (1, 0, 0, 1), (17, -31), 0x1000) if index % 3 == 0 else compound('base', matrix, offset, flags, parent_code)
    order = ['.notdef', 'base', 'part', 'root']
    font = FontBuilder(4096, isTTF=True)
    font.setupGlyphOrder(order)
    font.setupCharacterMap({65: 'root'})
    font.setupGlyf({'.notdef': TTGlyphPen(None).glyph(), 'base': base, 'part': part, 'root': root})
    font.setupHorizontalMetrics({name: (512 if name == 'base' else 700, 0) for name in order})
    font.setupHorizontalHeader(ascent=256, descent=-64)
    font.setupNameTable({'familyName': 'Composite Test', 'styleName': 'Regular',
                        'uniqueFontIdentifier': 'Composite Test', 'fullName': 'Composite Test',
                        'psName': 'CompositeTest'})
    font.setupOS2(sTypoAscender=256, sTypoDescender=-64, usWinAscent=256, usWinDescent=64)
    font.setupPost()
    font.setupMaxp()
    font.font['maxp'].maxStackElements = 64
    font.font['maxp'].maxTwilightPoints = 2
    font.save(path)
    with TTFont(path) as finished:
        finished['head'].flags &= ~2
        for name in ('part', 'root'):
            width, _ = finished['hmtx'].metrics[name]
            finished['hmtx'].metrics[name] = (width, finished['glyf'][name].xMin - (19 if index % 7 == 0 else 0))
        finished.save(path)


def main():
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(freetype.get_handle(), b'truetype', b'interpreter-version', ctypes.byref(version)) == 0
    matrices = ((1, 0, 0, 1), (.5, 0, 0, .5), (.5, 0, 0, .75),
                (1, .5, -.25, 1), (0, 1, -1, 0), (-1, 0, 0, -1), (.75, .25, .5, 1.25))
    codes = (b'', bytes([0xb1, 0, 97, 0x48]))
    parents = (b'', bytes([0xb1, 0, 128, 0x48, 0xb0, 1, 0xc0]))
    failures = []
    checked = 0
    scaled_differences = 0
    with tempfile.TemporaryDirectory(prefix='cupid-tt-composite-') as directory:
        path = Path(directory) / 'compound.ttf'
        for index, (matrix, offset, child, parent) in enumerate(itertools.product(matrices, ((0, 0), (33, -17), (100, 200)), codes, parents)):
            build(path, matrix, offset, child, parent, index)
            face = freetype.Face(str(path))
            pairs = ((13, 37), (37, 13), (24, 12)) if '--nonuniform' in sys.argv else ((13, 13), (37, 37), (64, 64))
            for x_ppem, ppem in pairs:
                mode = '--native-xy' if '--nonuniform' in sys.argv else '--native'
                sizes = [str(x_ppem), str(ppem)] if '--nonuniform' in sys.argv else [str(ppem)]
                result = subprocess.run([sys.argv[1], str(path), mode, *sizes, '3'],
                                        capture_output=True, text=True, timeout=20)
                assert result.returncode == 0, result.stderr
                lines = result.stdout.splitlines()
                assert list(map(int, lines[0].split()))[:4] == [1, 0, 0, 0], (index, ppem, result.stdout)
                native = [tuple(map(int, line.split())) for line in lines[1:]]
                face.set_pixel_sizes(x_ppem, ppem)
                face.load_glyph(3, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT | freetype.FT_LOAD_TARGET_MONO)
                actual = outline_state(face)
                if native != actual:
                    failures.append((index, ppem, matrix, offset, child.hex(), parent.hex(), native, actual))
                checked += 1
            del face
        # Components share private CVT/storage with siblings and their parent.
        writer = bytes([0xb1,0,97,0x44,0xb1,0,145,0x42])
        reader = bytes([0xb1,0,0,0x45,0x48,0xb1,1,0,0x43,0x48])
        for siblings in (False, True):
            build(path, (1,0,0,1), (0,0), writer, reader, 2)
            with TTFont(path) as font:
                font['cvt '] = newTable('cvt ')
                font['cvt '].values = array('h', [64])
                font['maxp'].maxStorage = 2
                if siblings:
                    font['glyf']['part'] = copy.deepcopy(font['glyf']['base'])
                    font['glyf']['part'].program.fromBytecode(reader)
                    extra = GlyphComponent()
                    extra.glyphName = 'part'
                    extra.x, extra.y, extra.flags = 200, 0, 0x1000
                    font['glyf']['root'].components.append(extra)
                font.save(path)
            result = subprocess.run([sys.argv[1], str(path), '--native', '64', '3'], capture_output=True, text=True, timeout=20)
            assert result.returncode == 0, result.stderr
            lines = result.stdout.splitlines()
            assert list(map(int, lines[0].split()))[:4] == [1,0,0,0], result.stdout
            native = [tuple(map(int, line.split())) for line in lines[1:]]
            face = freetype.Face(str(path))
            face.set_pixel_sizes(64,64)
            face.load_glyph(3, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT | freetype.FT_LOAD_TARGET_MONO)
            assert native == outline_state(face), (siblings, native, outline_state(face))
            assert native[:2] == [(97,35,1,0), (145,0,1,0)]
            if siblings:
                assert native[3:5] == [(297,35,1,0), (345,0,1,0)]
            checked += 1
            del face
        # OpenType requires the full matrix for a scaled component offset.
        build(path, (1, .5, -.25, 1), (100, 200), b'', b'', 2, scaled_offset=True)
        result = subprocess.run([sys.argv[1], str(path), '--native', '64', '3'],
                                capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stderr
        lines = result.stdout.splitlines()
        assert list(map(int, lines[0].split()))[:4] == [1, 0, 0, 0]
        native = [tuple(map(int, line.split())) for line in lines[1:]]
        expected = [(72, 301, 1, 0), (150, 300, 1, 0), (25, 350, 1, 1)]
        assert native == expected, native
        face = freetype.Face(str(path))
        face.set_pixel_sizes(64, 64)
        face.load_glyph(3, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT | freetype.FT_LOAD_TARGET_MONO)
        actual = outline_state(face)
        if actual != expected:
            assert actual == [(125, 275, 1, 0), (203, 274, 1, 0), (78, 324, 1, 1)], actual
            scaled_differences += 1
        checked += 1
        del face
    assert not failures, (len(failures), failures[:20])
    print(f'TrueType composite transforms: {checked - scaled_differences} independent matches, '
          f'{scaled_differences} specified-scaled-offset difference(s); engine {freetype.version()}; '
          f'{"nonuniform" if "--nonuniform" in sys.argv else "uniform"} transform sweep')


if __name__ == '__main__':
    main()
