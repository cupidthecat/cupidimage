#!/usr/bin/env python3
"""Compare simple-glyph instruction results using independently supplied geometry."""
import argparse
import ctypes
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import freetype
    from fontTools.ttLib import TTFont
except ImportError:
    print('TrueType glyph reference: skipped (optional fontTools/freetype-py unavailable)')
    raise SystemExit(0)

from generate_pdf_extended_fonts import FONTS, SOURCE

from pdf_tt_reference import outline_state


def fixture(font, name, ppem):
    glyph = font['glyf'][name]
    coordinates, ends, flags = glyph.getCoordinates(font['glyf'])
    count = len(coordinates)
    advance, bearing = font['hmtx'][name]
    if 'vmtx' in font:
        height, top_bearing = font['vmtx'][name]
        top = glyph.yMax + top_bearing
        bottom = top - height
    else:
        top = font['OS/2'].sTypoAscender
        bottom = font['OS/2'].sTypoDescender
    # Legacy monochrome phantom geometry. PDF layout does not use these advances.
    phantom = [(glyph.xMin - bearing, 0), (glyph.xMin - bearing + advance, 0),
               (0, top), (0, bottom)]
    units = font['head'].unitsPerEm
    scale = (ppem * 64 * 65536 + units // 2) // units

    def scaled(value):
        result = (abs(value * scale) + 32768) // 65536
        return -result if value < 0 else result

    points = []
    for i, (x, y) in enumerate(coordinates):
        sx, sy = scaled(x), scaled(y)
        points.append((sx, sy, sx, sy, 0, int(flags[i] & 1), int(i in ends), 0, x, y))
    for i, (x, y) in enumerate(phantom):
        sx, sy = scaled(x), scaled(y)
        cx = (sx + 32) // 64 * 64 if i < 2 else sx
        cy = (sy + 32) // 64 * 64 if i >= 2 else sy
        points.append((cx, cy, sx, sy, 0, 0, 0, 0, x, y))
    code = glyph.program.getBytecode()
    return (struct.pack('<III', count + 4, count, len(code)) +
            b''.join(struct.pack('<iiiiBBBBii', *point) for point in points) + code)


def main():
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(freetype.get_handle(), b'truetype', b'interpreter-version',
                                   ctypes.byref(version)) == 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe')
    parser.add_argument('--all-simple', action='store_true')
    parser.add_argument('--native', action='store_true')
    parser.add_argument('--gray', action='store_true')
    parser.add_argument('--nonuniform', action='store_true')
    options = parser.parse_args()
    if (options.gray or options.nonuniform) and not options.native:
        parser.error("--gray/--nonuniform require --native")
    if options.nonuniform and options.all_simple:
        parser.error("--nonuniform uses the selected six-pair sweep")
    all_simple = options.all_simple
    sizes = ((9, 16), (16, 9), (13, 37), (37, 13), (12, 24), (24, 12)) if options.nonuniform else tuple((n, n) for n in ((16,) if all_simple else (6, 9, 13, 16, 24, 37)))
    rejected = []
    checked = 0
    failures = []
    with tempfile.TemporaryDirectory(prefix='cupid-tt-glyphs-') as directory:
        path = Path(directory) / 'geometry.bin'
        for filename in FONTS.values():
            font_path = SOURCE / filename
            with TTFont(font_path) as font:
                face = freetype.Face(str(font_path))
                names = (font.getGlyphOrder() if all_simple else
                         [name for code, name in font.getBestCmap().items() if 32 <= code < 256])
                names = [name for name in names if not font['glyf'][name].isComposite() and
                         font['glyf'][name].numberOfContours > 0 and
                         hasattr(font['glyf'][name], 'program') and
                         font['glyf'][name].program.getBytecode()]
                for x_ppem, ppem in sizes:
                    face.set_pixel_sizes(x_ppem, ppem)
                    for name in names:
                        if options.native:
                            mode = ('--native-xy-gray' if options.gray else '--native-xy') if options.nonuniform else ('--native-gray' if options.gray else '--native')
                            dimensions = [str(x_ppem), str(ppem)] if options.nonuniform else [str(ppem)]
                            command = [options.probe, str(font_path), mode, *dimensions, str(font.getGlyphID(name))]
                        else:
                            path.write_bytes(fixture(font, name, ppem))
                            command = [options.probe, str(font_path), str(path), str(ppem)]
                        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
                        assert result.returncode == 0, (filename, name, ppem, result.stderr)
                        lines = result.stdout.splitlines()
                        status = list(map(int, lines[0].split()))
                        if all_simple and filename.startswith('LiberationMono-') and name == 'uni266B':
                            # At 16 ppem, rp1=33/rp2=32 exceed all 32 available
                            # points, including the four phantom points.
                            assert status == [0, 10, 0x39, 113, 32], (filename, name, status)
                            rejected.append((filename, name))
                            continue
                        assert status[:4] == [1, 0, 0, 0], (filename, name, ppem, status)
                        native = [tuple(map(int, line.split())) for line in lines[1:]]
                        if options.nonuniform:
                            del face
                            face = freetype.Face(str(font_path))
                            face.set_pixel_sizes(x_ppem, ppem)
                        face.load_glyph(font.getGlyphID(name), freetype.FT_LOAD_NO_BITMAP |
                                        freetype.FT_LOAD_NO_AUTOHINT | (freetype.FT_LOAD_TARGET_NORMAL if options.gray else freetype.FT_LOAD_TARGET_MONO))
                        actual = outline_state(face)
                        if native != actual:
                            failures.append((filename, name, ppem,
                                             [(i, a, b) for i, (a, b) in
                                              enumerate(zip(native, actual)) if a != b][:4]))
                        checked += 1
                del face
    assert not failures, (len(failures), failures[:20])
    assert len(rejected) == (4 if all_simple else 0), rejected
    print(f'TrueType simple glyph reference: {checked} outline matches at {len(sizes)} sizes; '
          f'{len(rejected)} specified-invalid IP rejections; engine {freetype.version()}; '
          f'{"native loading" if options.native else "supplied geometry"}; '
          f'{"grayscale" if options.gray else "monochrome"}; {"nonuniform" if options.nonuniform else "uniform"}; no composites or PDF rendering')


if __name__ == '__main__':
    main()
