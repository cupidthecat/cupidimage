#!/usr/bin/env python3
"""Compare bounded Type 1 fitting with independent outlines and grayscale bitmaps."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from compare_pdf_type1_hints import pfb_program
from test_pdf_type1 import fixture
from test_pdf_type1_charstrings import command, WIDTH


def authored_fonts():
    move = command([10, 0], 21)
    shape = command([100, 0], 5) + command([0, 720], 5) + command([-100, 0], 5) + b'\x09\x0e'
    stems = command([0, 20], 1) + command([700, 20], 1)
    zones = '/BlueValues [-20 0 700 720] def\n'
    fonts = []

    def add(name, code, extra=zones, **kwargs):
        fonts.append((name, fixture(glyphs=[('.notdef', WIDTH+b'\x0e'), ('A', code)],
                                    extra=extra.encode(), **kwargs)))

    for name, extra in (
        ('default', ''), ('blues', zones),
        ('family', zones+'/FamilyBlues [-10 0 680 700] def\n'),
        ('other', zones+'/OtherBlues [-220 -200] def\n'),
        ('language', '/LanguageGroup 1 def\n'),
        ('overshoot', zones+'/BlueScale .01 def /BlueShift 10 def /BlueFuzz 2 def\n'),
    ):
        add(name, WIDTH+stems+move+shape, extra)
    for axis in (1, 3):
        for width in (-20, -21, 0, 20):
            add(f'edge-{axis}-{width}', WIDTH+command([700, width], axis)+move+shape)
    for op in ((12, 1), (12, 2)):
        add(f'triple-{op}', WIDTH+command([90, 10, 10, 10, 50, 10], op)+move+shape)
    replacement = command([0, 1, 3], (12, 16)) + bytes([12, 17, 10])
    subrs = [command([0, 30], 1)+command([680, 40], 1)+b'\x0b']
    add('replace', WIDTH+stems+move+command([100, 0], 5)+replacement+
        command([0, 720], 5)+command([-100, 0], 5)+b'\x09\x0e', subrs=subrs)
    for index, extra in enumerate(('', zones)):
        for part, ops in enumerate((shape.replace(b'\x09', b''),
                                   command([100, 0], 5)+b'\x0e',
                                   command([100, 0], 5)+command([20, 20], 21)+command([30, 40], 5)+b'\x0e',
                                   command([25, 0, 25, 50, 50, 0], 8)+b'\x0e')):
            add(f'open-{index}-{part}', WIDTH+stems+move+ops, extra)
    add('replace-at-open-end', WIDTH+stems+move+command([100, 0], 5)+replacement+b'\x0e', subrs=subrs)
    # Logical current-point changes must not introduce a new painted vertex.
    code = WIDTH+move+command([100, 0], 5)
    for value in (200, 0):
        code += command([value, 1, 3], (12, 16))+bytes([12, 17])
    add('currentpoint', code+bytes([12, 33])+command([0, 100], 5)+command([-190, -100], 5)+b'\x09\x0e')
    base = command([20, 600], 13)+command([0, 20], 1)+command([580, 20], 1)+command([0, 0], 21)
    base += command([400, 0], 5)+command([0, 600], 5)+command([-400, 0], 5)+b'\x09\x0e'
    accent = command([10, 200], 13)+command([0, 20], 1)+command([80, 20], 1)+command([0, 0], 21)
    accent += command([100, 0], 5)+command([0, 100], 5)+command([-100, 0], 5)+b'\x09\x0e'
    for x, y in ((100, 600), (-20, -200), (15, 599)):
        composite = command([20, 600], 13)+command([10, x, y, 65, 194], (12, 6))
        fonts.append((f'composite-{x}-{y}', fixture(glyphs=[('.notdef', WIDTH+b'\x0e'),
                      ('A', base), ('acute', accent), ('Aacute', composite)], extra=zones.encode())))
    composite = command([20, 600], 13)+command([10, 41, 4], (12, 12))
    composite += command([1199, 2], (12, 12))+command([65, 194], (12, 6))
    fonts.append(('composite-fractional', fixture(glyphs=[('.notdef', WIDTH+b'\x0e'),
                  ('A', base), ('acute', accent), ('Aacute', composite)], extra=zones.encode())))
    return fonts


def fill_contours(segments):
    """Normalize only implicit fill closure, keeping native open strokes testable separately."""
    result = []
    start = None
    for segment in segments:
        if segment[0] == 0:
            start = segment[1:3]
        if segment[0] == 4:
            if result[-1][-2:] != start:
                result.append((1, *start))
            result.append((3,))
        else:
            result.append(segment)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--fonts', type=Path, nargs='*', default=[])
    parser.add_argument('--nonuniform', action='store_true')
    parser.add_argument('--bitmaps', action='store_true')
    args = parser.parse_args()
    try:
        import freetype
    except ImportError:
        print('Type 1 fitting references skipped: optional freetype-py unavailable')
        return
    fonts = [(name, parts, parts[0]) for name, parts in authored_fonts()]
    paths = sorted({p for item in args.fonts for p in
                    (item.glob('*.pfb') if item.is_dir() else [item])})
    for font in paths:
        data = font.read_bytes()
        fonts.append((font.name, pfb_program(data), data))
    sizes = ([(8, 12), (12, 8), (16, 24), (24, 16), (48, 72), (72, 48)] if args.nonuniform else
             [(v, v) for v in (8, 12, 16, 24, 48, 72)])
    if args.bitmaps:
        sizes = [(16, 16), (48, 48)]
    matches = bitmaps = 0
    with tempfile.TemporaryDirectory(prefix='cupid-type1-fit-reference-') as tmp:
        path = Path(tmp)/'font.bin'
        for name, (data, clear, encrypted), reference_data in fonts:
            path.write_bytes(data)
            face = freetype.Face.from_bytes(reference_data)
            assert face.units_per_EM == 1000, (name, face.units_per_EM)
            for x, y in sizes:
                extra = ['--bitmaps'] if args.bitmaps else []
                lines = subprocess.check_output([str(args.probe.resolve()), str(path), str(clear),
                                                str(encrypted), str(x), str(y), *extra],
                                               text=True, timeout=60).splitlines()
                actual, masks = {}, {}
                glyph = None
                for line in lines:
                    tokens = line.split()
                    if tokens[0] == 'glyph':
                        glyph = tokens[1]
                        assert tokens[2] == '1', (name, x, y, line)
                        actual[glyph] = []
                    elif tokens[0] == 'segment':
                        actual[glyph].append(tuple(map(int, tokens[1:])))
                    elif tokens[0] == 'bitmap':
                        assert tokens[1] == '1', (name, x, y, glyph, line)
                        masks[glyph] = (tuple(map(int, tokens[2:6])), bytes.fromhex(''.join(tokens[6:])))
                assert len(actual) == face.num_glyphs
                face.set_pixel_sizes(x, y)
                for gid in range(face.num_glyphs):
                    glyph = face.get_glyph_name(gid).decode('ascii')
                    face.load_glyph(gid, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT)
                    expected = []

                    def move(point, _):
                        if expected:
                            expected.append((3,))
                        expected.append((0, point.x, point.y))

                    def line(point, _):
                        expected.append((1, point.x, point.y))

                    def cubic(a, b, c, _):
                        expected.append((2, a.x, a.y, b.x, b.y, c.x, c.y))

                    face.glyph.outline.decompose(None, move_to=move, line_to=line, cubic_to=cubic)
                    if expected:
                        expected.append((3,))
                    assert fill_contours(actual[glyph]) == expected, (name, x, y, glyph, actual[glyph], expected)
                    matches += 1
                    if args.bitmaps:
                        face.glyph.render(freetype.FT_RENDER_MODE_NORMAL)
                        bitmap = face.glyph.bitmap
                        metrics = (face.glyph.bitmap_left, face.glyph.bitmap_top, bitmap.width, bitmap.rows)
                        buf = bytes(bitmap.buffer)
                        pixels = b''.join(buf[i*bitmap.pitch:i*bitmap.pitch+bitmap.width] for i in range(bitmap.rows))
                        assert masks[glyph] == (metrics, pixels), (name, x, y, glyph, masks[glyph][0], metrics)
                        bitmaps += 1
    print(f'Type 1 fitting: {matches} independent outline matches, {bitmaps} exact grayscale bitmaps; '
          f'{len(fonts)} fonts; {"nonuniform" if args.nonuniform else "uniform"}; engine {freetype.version()}; '
          'standalone glyph checks, not PDF page integration')


if __name__ == '__main__':
    main()
