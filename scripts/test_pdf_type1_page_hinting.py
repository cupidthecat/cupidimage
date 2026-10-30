#!/usr/bin/env python3
"""Check Type 1 page fitting, placement, PDF advances and glyph coverage."""
import argparse
import itertools
import math
from pathlib import Path
import struct
import subprocess
import tempfile
from test_pdf_type1 import fixture as font_program
from test_pdf_type1_charstrings import command, WIDTH
from test_pdf_embedded_fonts import fixture as page
from test_pdf_type3 import stream
from compare_pdf_type1_hints import pfb_program

RATIO = struct.unpack('f', struct.pack('f', 96/72))[0]
BASE = WIDTH+command([0, 20], 1)+command([580, 20], 1)+command([0, 0], 21)
BASE += command([400, 0], 5)+command([0, 600], 5)+command([-400, 0], 5)+b'\x09\x0e'
ACCENT = command([0, 200], 13)+command([0, 20], 1)+command([80, 20], 1)+command([0, 0], 21)
ACCENT += command([100, 0], 5)+command([0, 100], 5)+command([-100, 0], 5)+b'\x09\x0e'
COMPOSITE = WIDTH+command([0, 100, 600, 65, 194], (12, 6))
GLYPHS = [('.notdef', WIDTH+b'\x0e'), ('space', WIDTH+b'\x0e'), ('A', BASE),
          ('acute', ACCENT), ('Aacute', COMPOSITE)]
PRIVATE = b'/BlueValues [-20 0 600 620 700 720] def\n'
ENCODING = '<< /BaseEncoding /WinAnsiEncoding /Differences [66 /Aacute] >>'
METRICS = '/FirstChar 65 /LastChar 66 /Widths [500 700]'


def font(**kwargs):
    kwargs.setdefault('glyphs', GLYPHS)
    kwargs.setdefault('extra', PRIVATE)
    return font_program(**kwargs)


def pdf(content, program=None, encoding=ENCODING, metrics=METRICS, extra=None, **kwargs):
    data, clear, encrypted = program or font(**kwargs)
    objects = {
        5: ('<< /Type /Font /Subtype /Type1 /BaseFont /CupidShapes /FontDescriptor 8 0 R '
            '/Encoding '+encoding+' '+metrics+' >>').encode(),
        8: b'<< /Type /FontDescriptor /FontName /CupidShapes /Flags 32 /FontBBox [0 -200 1000 1000] '
           b'/Ascent 800 /Descent -200 /ItalicAngle 0 /CapHeight 700 /StemV 80 /FontFile 9 0 R >>',
        9: stream(data, f'/Length1 {clear} /Length2 {encrypted} /Length3 0'),
    }
    objects.update(extra or {})
    return page(content, extra=objects)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    parser.add_argument('allocator', type=Path)
    parser.add_argument('cli', type=Path, nargs='?')
    parser.add_argument('--fonts', type=Path, nargs='*', default=[])
    args = parser.parse_args()
    probe, allocator = str(args.probe.resolve()), str(args.allocator.resolve())
    checks = allocations = references = 0
    with tempfile.TemporaryDirectory(prefix='cupid-type1-page-hints-') as tmp:
        path = Path(tmp)/'case.pdf'

        def render(content, aa=1, hint=1, **kwargs):
            nonlocal checks
            path.write_bytes(pdf(content, **kwargs))
            result = subprocess.run([probe, str(path), '0', str(aa), '0', '0', str(hint)],
                                    capture_output=True, timeout=30)
            assert result.returncode == 0, (result.returncode, result.stderr, content)
            checks += 1
            return result.stdout[12:]

        def allocation_check(aa=1, fail=0):
            nonlocal allocations
            result = subprocess.run([allocator, str(path), str(fail), str(aa), '0', '0', '1'],
                                    capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, (result.stdout, result.stderr)
            allocations += int(result.stdout.split('checks')[0].split()[-1])

        for aa, char, size in itertools.product((0, 1), ('A', 'B'), (12, 18, 30)):
            content = f'1 0 0 rg BT /F {size} Tf 20.1875 30.1875 Td ({char}) Tj ET'
            hinted = render(content, aa)
            if aa:
                assert hinted != render(content, aa, 0), (char, size, 'hinting had no effect')
            advance = .5 if char == 'A' else .7
            automatic = f'BT /F {size} Tf 20.1875 30.1875 Td ({char}{char}) Tj ET'
            explicit = (f'BT /F {size} Tf 20.1875 30.1875 Td ({char}) Tj '
                        f'1 0 0 1 {20.1875+size*advance} 30.1875 Tm ({char}) Tj ET')
            assert render(automatic, aa) == render(explicit, aa), 'hinting changed PDF advances'
            path.write_bytes(pdf(automatic))
            allocation_check(aa)
        widths = '/FirstChar 32 /LastChar 66 /Widths ['+' '.join(str(600 if i==32 else 500 if i==65 else 700 if i==66 else 0) for i in range(32,67))+']'
        for aa in (0, 1):
            state = 'BT /F 18 Tf 2 Tc 3 Tw 80 Tz 4 Ts 20.1875 30.1875 Td '
            assert render(state+'(A A) Tj ET', aa, metrics=widths) == render(
                state+'(A) Tj 1 0 0 1 41.6275 30.1875 Tm (A) Tj ET', aa, metrics=widths)
            assert render('BT /F 18 Tf 20.1875 30.1875 Td [(A) 100 (B)] TJ ET', aa) == render(
                'BT /F 18 Tf 20.1875 30.1875 Td (A) Tj 1 0 0 1 27.3875 30.1875 Tm (B) Tj ET', aa)
            assert render("BT /F 18 Tf 12 TL 20.1875 48.1875 Td (A) Tj (B) ' ET", aa) == render(
                'BT /F 18 Tf 20.1875 48.1875 Td (A) Tj 1 0 0 1 20.1875 36.1875 Tm (B) Tj ET', aa)
        for aa, mode in itertools.product((0, 1), (0, 1, 2)):
            content = f'1 w BT /F 18 Tf {mode} Tr 20.1875 30.1875 Td (AB) Tj ET'
            assert render(content, aa) == render(content.replace(f'{mode} Tr', f'{mode+4} Tr'), aa)
        for matrix, size, tm, tx, ty in (
            ('[.002 0 0 .002 0 0]', 9, (1, 0, 0, 1), 20.1875, 30.1875),
            ('[.0015 0 0 .0015 0 0]', 12, (1, 0, 0, 1), 20.1875, 30.1875),
            ('[.001 0 0 .001 .1 .2]', 18, (1, 0, 0, 1), 21.9875, 33.7875),
            ('[0 .001 -.001 0 0 0]', 18, (0, 1, -1, 0), 20.1875, 30.1875),
            ('[.001 0 0 -.001 0 0]', 18, (1, 0, 0, -1), 20.1875, 30.1875),
            ('[.001 0 .0002 .001 0 0]', 18, (1, 0, .2, 1), 20.1875, 30.1875),
        ):
            actual = render(f'BT /F {size} Tf 20.1875 30.1875 Td (A) Tj ET', matrix=matrix)
            expected = render('BT /F 18 Tf '+' '.join(map(str, (*tm, tx, ty)))+' Tm (A) Tj ET')
            assert actual == expected, ('FontMatrix', matrix)
        for matrix, mode in itertools.product(('[.001 0 0 0 0 0]', '[0 0 0 0 0 0]'), (0, 1, 2, 7)):
            content = f'BT /F 18 Tf {mode} Tr 20 30 Td (A) Tj ET 1 0 0 rg 0 0 120 90 re f'
            assert render(content, matrix=matrix) == render(content, hint=0, matrix=matrix)
        bad = font(extra=PRIVATE+b'/BlueScale 1 25 div def\n')
        path.write_bytes(pdf('BT /F 18 Tf 20 30 Td (A) Tj ET', program=bad))
        result = subprocess.run([probe, str(path), '0', '--pdf-type1-hinting'], capture_output=True)
        assert result.returncode == 3 and not result.stdout
        allocation_check(fail=1)
        for state in ('/F 0 Tf', '/F 18 Tf 0 Tz', '/F 18 Tf 3 Tr'):
            render(f'BT {state} 20 30 Td (A) Tj ET', program=bad)
        for invalid in (-1, 2):
            result = subprocess.run([probe, str(path), '0', '1', '0', '0', str(invalid)], capture_output=True)
            assert result.returncode == 3 and not result.stdout
            checks += 1
        from test_pdf_forms import form
        data, clear, encrypted = font()
        local = b'<< /Type /Font /Subtype /Type1 /FontDescriptor 13 0 R /Encoding << /Differences [66 /Aacute] >> /FirstChar 66 /LastChar 66 /Widths [700] >>'
        descriptor = b'<< /Type /FontDescriptor /FontName /CupidShapes /Flags 32 /FontBBox [0 -200 1000 1000] /Ascent 800 /Descent -200 /ItalicAngle 0 /CapHeight 700 /StemV 80 /FontFile 14 0 R >>'
        parent = b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R /H 12 0 R >> /XObject << /G 20 0 R >> >> >>'
        content = 'BT /F 18 Tf 60 30 Td (B) Tj ET'
        objects = {3: parent, 12: local, 13: descriptor,
                   14: stream(data, f'/Length1 {clear} /Length2 {encrypted} /Length3 0'),
                   20: form(content, '/BBox [0 0 120 90] /Resources << /Font << /F 12 0 R >> >>')}
        for aa in (0, 1):
            assert render('/G Do', aa, extra=objects) == render(content.replace('/F ', '/H '), aa, extra=objects)
            path.write_bytes(pdf('/G Do', extra=objects))
            allocation_check(aa)
        if args.cli:
            from test_sixel import decode
            expected = render('1 0 0 rg BT /F 18 Tf 20.1875 30.1875 Td (AB) Tj ET')
            wanted = [tuple(expected[i+c]*100//255 for c in range(3)) for i in range(0, len(expected), 4)]
            for name in ('case.pdf', 'case.PdF', 'extensionless'):
                target = Path(tmp)/name
                target.write_bytes(path.read_bytes())
                result = subprocess.run([str(args.cli.resolve()), '--sixel', '--sixel-dither', 'none',
                                         '--pdf-text-aa', '--pdf-type1-hinting', str(target), '160', '120'],
                                        capture_output=True, check=True, timeout=30)
                transparent, dimensions, pixels = decode(result.stdout)
                assert not transparent and dimensions == (160, 120) and pixels == wanted
                checks += 1
        try:
            import freetype
            from test_pdf_coverage_reference import reference_coverage
        except (ImportError, SystemExit):
            freetype = None
        if freetype:
            def reference(face, glyph, size, tm, tx, ty):
                a, b, c, d = tm
                ppem = max(1, math.floor(math.hypot(c, d)*size*RATIO+.5))
                fixed = [math.trunc(v*size*RATIO/ppem*65536) for v in (a, b, c, d)]
                ox, oy = round((tx+1.125)*RATIO*64), round((90-ty)*RATIO*64)
                face.set_pixel_sizes(ppem, ppem)
                face.set_transform(freetype.Matrix(fixed[0], fixed[2], fixed[1], fixed[3]),
                                   freetype.Vector(ox, 120*64-oy))
                face.load_glyph(glyph, freetype.FT_LOAD_NO_BITMAP|freetype.FT_LOAD_NO_AUTOHINT)
                outline = face.glyph.outline
                ends = set(outline.contours)
                points = [(x, y, tag&3, int(i in ends)) for i, ((x, y), tag) in
                          enumerate(zip(outline.points, outline.tags))]
                mask = reference_coverage(points, 160, 120)
                expected = bytearray(b'\xff'*(160*120*4))
                for y in range(120):
                    for x in range(160):
                        alpha = mask[(119-y)*160+x]
                        expected[(y*160+x)*4+1:(y*160+x)*4+3] = bytes((255-alpha, 255-alpha))
                return bytes(expected)

            matrices = ((1, 0, 0, 1), (0, 1, -1, 0), (-1, 0, 0, 1), (1, .25, -.125, 1), (.8, -.2, .3, 1))
            for char, size, tm in itertools.product(('A', 'B'), (12, 18, 30), matrices):
                face = freetype.Face.from_bytes(data)
                glyph = face.get_name_index(b'A' if char == 'A' else b'Aacute')
                expected = reference(face, glyph, size, tm, 40.1875, 35.1875)
                actual = render(f'1 0 0 rg BT /F {size} Tf '+' '.join(map(str, (*tm, 40.1875, 35.1875)))+f' Tm ({char}) Tj ET')
                assert actual == expected, (char, size, tm, sum(a!=b for a,b in zip(actual,expected)))
                references += 1
            for char in ('A', 'B'):
                face = freetype.Face.from_bytes(data)
                reference(face, face.get_name_index(b'A' if char == 'A' else b'Aacute'),
                          18, matrices[0], 20.1875, 30.1875)
                outline = face.glyph.outline
                commands, first = [], 0
                for last in outline.contours:
                    assert all((tag&3)==1 for tag in outline.tags[first:last+1])
                    for i, (x,y) in enumerate(outline.points[first:last+1]):
                        px,py = x/64/RATIO-1.125,90-(120-y/64)/RATIO
                        commands.append(f'{px:.12g} {py:.12g} '+('m' if i==0 else 'l'))
                    commands.append('h')
                    first=last+1
                geometry=' '.join(commands)
                glyph=f'BT /F 18 Tf MODE Tr 20.1875 30.1875 Td ({char}) Tj ET'
                for style in ('1 w', '1 w 1 J 1 j', '1 w 2 J 2 j [2 1] .5 d'):
                    assert render(style+' '+glyph.replace('MODE','1')) == render(style+' '+geometry+' S')
                paint='1 0 0 rg 0 0 120 90 re f'
                assert render(glyph.replace('MODE','7')+' '+paint) == render(geometry+' W n '+paint)
            paths = sorted({p for item in args.fonts for p in (item.glob('*.pfb') if item.is_dir() else [item])})
            for source in paths:
                raw = source.read_bytes()
                program = pfb_program(raw)
                for char, size, tm in itertools.product(('A', 'g'), (12, 24), matrices):
                    face = freetype.Face.from_bytes(raw)
                    glyph = face.get_name_index(char.encode())
                    if not glyph:
                        continue
                    expected = reference(face, glyph, size, tm, 40.1875, 35.1875)
                    actual = render(f'1 0 0 rg BT /F {size} Tf '+' '.join(map(str, (*tm, 40.1875, 35.1875)))+f' Tm ({char}) Tj ET',
                                    program=program, encoding='/WinAnsiEncoding',
                                    metrics=f'/FirstChar {ord(char)} /LastChar {ord(char)} /Widths [600]')
                    assert actual == expected, (source.name, char, size, tm)
                    references += 1
    print(f'Type 1 page hinting: {checks} page/API/CLI checks; {allocations} allocation/cache checks; '
          f'{references} independent grayscale pages')


if __name__ == '__main__':
    main()
