#!/usr/bin/env python3
"""Compare bounded CFF grid fitting with independently produced device outlines."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from generate_pdf_standard_outlines import FONTS, ROOT, font_bytes
from test_pdf_cff import fixture, real
from test_pdf_cff_hints import entry as wide_entry
from test_pdf_type2 import command, number


def authored_fonts(wide=False):
    def entry(field, values):
        if wide:
            return wide_entry(field, values)
        return b''.join(number(v) if isinstance(v, int) else real(v) for v in values) + wide_entry(field, [])

    move = command([10, 0], 21)
    shape = command([100, 0, 0, 720, -100, 0], 5) + bytes([14])
    programs = []
    zones = entry(0, [-20, 20, 700, 20])
    families = entry(2, [-10, 10, 680, 20])
    other = entry(1, [-220, 20]) + entry(3, [-210, 10])
    stems = command([0, 20, 680, 20], 18)
    for name, private in [('default', b''), ('blues', zones), ('families', zones + families),
                          ('other', zones + families + other),
                          ('language', entry(12, [1])),
                          ('em-box', entry(0, [-250, 0, 1350, 0]) + entry(12, [1])),
                          ('overshoot', zones + entry(6, [.01]) + entry(7, [10]) + entry(8, [2]))]:
        programs.append((name, private, stems + move + shape))
    for width in (-20, -21, 0, 20):
        programs.append((f'edge-{width}', zones, command([700, width], 18) + move + shape))
    programs += [
        ('empty-mask-recovery', zones, bytes([19]) + move + shape),
        ('empty-counter-recovery', zones, bytes([20]) + move + shape),
        ('mask-padding-recovery', zones, stems + bytes([19, 0x81]) + move + shape),
        ('active-mask', zones, stems + bytes([19, 0x80]) + move + shape),
        ('inactive-mask', zones, stems + bytes([19, 0]) + move + shape),
        ('changing-mask', zones, stems + bytes([19, 0x80]) + move + command([100, 0], 5) +
         bytes([19, 0x40]) + command([0, 720, -100, 0], 5) + bytes([14])),
        ('counter', zones, command([0, 20, 80, 20, 580, 20], 18) + bytes([20, 0xe0, 19, 0xe0]) + move + shape),
        ('vertical', zones, stems + command([10, 20, 50, 20], 23) + bytes([19, 0xf0]) + move + shape),
    ]
    accent = command([0, 20, 60, 20], 18) + command([0, 0], 21) + command([100, 0, 0, 100, -100, 0], 5) + bytes([14])
    base = command([0, 20, 580, 20], 18) + command([0, 0], 21) + command([400, 0, 0, 600, -400, 0], 5) + bytes([14])
    composite_fonts = []
    for x, y in [(100, 600), (10.25, 599.5), (-20, -200)]:
        code = command([x, y, 65, 194], 14)
        composite_fonts.append((f'composite-{x}-{y}', fixture(glyphs=5, charset=b'\0\0\1\0\x22\0\x7d\0\xab', strings=(),
            programs=[bytes([14]), bytes([14]), base, accent, code], private_prefixes=[zones + families + other])))
    cid_font = fixture(glyphs=3, cid=True, private_count=2, fdselect=b'\0\0\0\1', charset=b'\0\0\1\0\2',
        programs=[bytes([14]), base, base], private_prefixes=[zones, zones + families + other])
    composite_fonts.append(('cid-private-dictionaries', cid_font))
    return [(name, fixture(charset=0, strings=(), glyphs=2, programs=[bytes([14]), code],
                           private_prefixes=[private])) for name, private, code in programs] + composite_fonts



def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('probe', type=Path)
    parser.add_argument('--nonuniform', action='store_true')
    parser.add_argument('--bitmaps', action='store_true')
    args = parser.parse_args()
    try:
        import freetype
    except ImportError:
        print('CFF grid-fitting reference checks skipped: optional freetype-py unavailable')
        return
    sizes = [(8, 12), (12, 8), (16, 24), (24, 16), (48, 72), (72, 48)] if args.nonuniform else [
        (v, v) for v in (8, 12, 16, 24, 48, 72)]
    if args.bitmaps:
        sizes = [(16, 16), (48, 48)]
    bitmap_checks = 0
    extra = ['--bitmaps'] if args.bitmaps else []
    matches = 0
    encoding_checks = 0
    wide_fonts = dict(authored_fonts(wide=True))
    with tempfile.TemporaryDirectory(prefix='cupid-ps-gridfit-') as tmp:
        path = Path(tmp) / 'font.cff'
        fonts = [(name, font_bytes(stem)) for name, stem in FONTS.items()]
        fonts += [(p.name, p.read_bytes()) for p in sorted((ROOT / 'third_party/standard-spacing-marks').glob('*.cff'))]
        fonts += authored_fonts()
        for name, data in fonts:
            path.write_bytes(data)
            face = freetype.Face(str(path))
            assert face.units_per_EM == 1000, name
            for x_size, y_size in sizes:
                lines = subprocess.check_output([str(args.probe.resolve()), str(path),
                                                 str(x_size), str(y_size)] + extra, text=True,
                                                timeout=30).splitlines()
                actual = {}
                bitmaps = {}
                for line in lines:
                    fields = line.split()
                    if fields[0] == 'glyph':
                        gid = int(fields[1])
                        assert fields[2] == '1', (name, x_size, y_size, line)
                        actual[gid] = []
                    elif fields[0] == 'bitmap':
                        assert fields[1] == '1', (name, x_size, y_size, gid, line)
                        bitmaps[gid] = (tuple(map(int, fields[2:6])), bytes.fromhex(''.join(fields[6:])))
                    else:
                        kind = int(fields[1])
                        actual[gid].append((kind, *map(int, fields[2:])) if kind != 3 else (3,))
                if name in wide_fonts:
                    wide_path = Path(tmp) / 'wide.cff'
                    wide_path.write_bytes(wide_fonts[name])
                    wide_lines = subprocess.check_output([str(args.probe.resolve()), str(wide_path),
                                                          str(x_size), str(y_size)] + extra, text=True,
                                                         timeout=30).splitlines()
                    assert wide_lines == lines, (name, x_size, y_size, 'DICT integer encoding changed native fitting')
                    encoding_checks += 1
                face.set_pixel_sizes(x_size, y_size)
                assert len(actual) == face.num_glyphs
                for gid in range(face.num_glyphs):
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
                    assert actual[gid] == expected, (name, x_size, y_size, gid, actual[gid], expected)
                    matches += 1
                    if args.bitmaps:
                        face.glyph.render(freetype.FT_RENDER_MODE_NORMAL)
                        bitmap = face.glyph.bitmap
                        metrics = (face.glyph.bitmap_left, face.glyph.bitmap_top, bitmap.width, bitmap.rows)
                        data = bytes(bitmap.buffer)
                        expected_pixels = b''.join(data[y*bitmap.pitch:y*bitmap.pitch+bitmap.width] for y in range(bitmap.rows))
                        assert bitmaps[gid] == (metrics, expected_pixels), (name, x_size, y_size, gid, bitmaps[gid][0], metrics)
                        bitmap_checks += 1
    print(f'CFF grid fitting: {matches} independent outline matches; '
          f'{"nonuniform" if args.nonuniform else "uniform"} sizing; engine {freetype.version()}; '
          f'{encoding_checks} native DICT-width equivalence checks; {bitmap_checks} exact grayscale bitmaps; '
          'standalone glyph checks; page tests are separate')


if __name__ == '__main__':
    main()
