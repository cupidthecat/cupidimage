#!/usr/bin/env python3
"""Check CFF page hinting, PDF metrics, ownership, and independent glyph coverage."""
import itertools
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_cff_render import fixture
from test_pdf_ps_gridfit_reference import authored_fonts
from generate_pdf_standard_outlines import FONTS, font_bytes

RATIO = struct.unpack('f', struct.pack('f', 96 / 72))[0]
FONTS_AUTHORED = dict(authored_fonts())
FONT = FONTS_AUTHORED['composite-100-600']
ENCODING = '<< /BaseEncoding /WinAnsiEncoding /Differences [66 /Aacute] >>'
METRICS = '/FirstChar 65 /LastChar 66 /Widths [500 700]'


def pdf(content, font=FONT, **kwargs):
    kwargs.setdefault('encoding', ENCODING)
    kwargs.setdefault('metrics', METRICS)
    return fixture(content, font=font, **kwargs)


def main():
    probe, allocator = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    checks = allocations = references = 0
    with tempfile.TemporaryDirectory(prefix='cupid-cff-page-hints-') as directory:
        path = Path(directory) / 'case.pdf'

        def render(content, aa=1, hint=1, **kwargs):
            nonlocal checks
            path.write_bytes(pdf(content, **kwargs))
            result = subprocess.run([probe, str(path), '0', str(aa), '0', str(hint)],
                                    capture_output=True, timeout=20)
            assert result.returncode == 0, (result.returncode, result.stderr, content)
            checks += 1
            return result.stdout[12:]

        def allocation_check(aa=1, fail=0):
            nonlocal allocations
            result = subprocess.run([allocator, str(path), str(fail), str(aa), '0', '1'],
                                    capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, (result.stdout, result.stderr)
            allocations += int(result.stdout.split('checks')[0].split()[-1])

        for aa, char, size in itertools.product((0, 1), ('A', 'B'), (12, 18, 30)):
            content = f'1 0 0 rg BT /F {size} Tf 20.1875 30.1875 Td ({char}) Tj ET'
            hinted = render(content, aa)
            if aa:
                assert hinted != render(content, aa, 0), (char, size, 'hints had no effect')
            advance = .5 if char == 'A' else .7
            automatic = f'BT /F {size} Tf 20.1875 30.1875 Td ({char}{char}) Tj ET'
            explicit = (f'BT /F {size} Tf 20.1875 30.1875 Td ({char}) Tj '
                        f'1 0 0 1 {20.1875 + size * advance} 30.1875 Tm ({char}) Tj ET')
            assert render(automatic, aa) == render(explicit, aa), 'hinting changed PDF advances'
            path.write_bytes(pdf(automatic))
            allocation_check(aa)
        for aa, vertical in itertools.product((0, 1), (False, True)):
            metrics = '/DW 600 /W [1 [400 700]] /W2 [1 [-1100 200 900 -800 350 950]]'
            automatic = 'BT /F 18 Tf 60 70 Td <00010002> Tj ET'
            tx, ty = (60, 50.2) if vertical else (67.2, 70)
            explicit = f'BT /F 18 Tf 60 70 Td <0001> Tj 1 0 0 1 {tx} {ty} Tm <0002> Tj ET'
            kwargs = dict(font=FONTS_AUTHORED['cid-private-dictionaries'], cid=True,
                          vertical=vertical, metrics=metrics)
            assert render(automatic, aa, **kwargs) == render(explicit, aa, **kwargs)
        for aa, mode in itertools.product((0, 1), (0, 1, 2)):
            a = f'1 w BT /F 18 Tf {mode} Tr 20.1875 30.1875 Td (AB) Tj ET'
            b = a.replace(f'{mode} Tr', f'{mode + 4} Tr')
            assert render(a, aa) == render(b, aa), 'deferred clipping changed glyph paint'
        # FontMatrix belongs to glyph placement, not PDF advances.
        from test_pdf_cff import fixture as cff_font
        from test_pdf_cff_hints import entry
        from test_pdf_type2 import command
        base = command([0, 20, 580, 20], 18) + command([0, 0], 21) + command([400, 0, 0, 600, -400, 0], 5) + b'\x0e'
        def matrix_font(matrix=None):
            return cff_font(programs=[b'\x0e', b'\x0e', base, b'\x0e'], matrix=matrix,
                            private_prefixes=[entry(0, [0, 0, 600, 20])])
        plain = matrix_font()
        for matrix, size, tm, tx, ty in [
                ((.002, 0, 0, .002, 0, 0), 9, (1, 0, 0, 1), 20.1875, 30.1875),
                ((.0015, 0, 0, .0015, 0, 0), 12, (1, 0, 0, 1), 20.1875, 30.1875),
                ((.001, 0, 0, .001, .1, .2), 18, (1, 0, 0, 1), 21.9875, 33.7875),
                ((0, .001, -.001, 0, 0, 0), 18, (0, 1, -1, 0), 20.1875, 30.1875),
                ((.001, 0, 0, -.001, 0, 0), 18, (1, 0, 0, -1), 20.1875, 30.1875),
                ((.001, 0, .0002, .001, 0, 0), 18, (1, 0, .2, 1), 20.1875, 30.1875)]:
            actual = render(f'BT /F {size} Tf 20.1875 30.1875 Td (A) Tj ET', font=matrix_font(matrix))
            expected = render('BT /F 18 Tf ' + ' '.join(map(str, (*tm, tx, ty))) + ' Tm (A) Tj ET', font=plain)
            assert actual == expected, ('FontMatrix', matrix, sum(a != b for a, b in zip(actual, expected)))
        for matrix, mode in itertools.product(((.001, 0, 0, 0, 0, 0), (0, 0, 0, 0, 0, 0)), (0, 1, 2, 7)):
            content = f'BT /F 18 Tf {mode} Tr 20 30 Td (A) Tj ET 1 0 0 rg 0 0 120 90 re f'
            assert render(content, font=matrix_font(matrix)) == render(content, hint=0, font=matrix_font(matrix))
        # CID Font DICT matrices stand alone unless a top matrix is explicit.
        cid_programs = [b'\x0e', base, base]
        def cid_matrix_font(top=None, subsidiary=None):
            return cff_font(glyphs=3, cid=True, private_count=2, fdselect=b'\0\0\0\1',
                            charset=b'\0\0\1\0\2', programs=cid_programs, matrix=top,
                            fd_matrices=[None, subsidiary])
        matrix_cases = [
            (None, (.001, 0, 0, .001, 0, 0), (.001, 0, 0, .001, 0, 0)),
            (None, (.002, 0, 0, .002, 0, 0), (.002, 0, 0, .002, 0, 0)),
            (None, (.001, .0002, 0, .001, 0, 0), (.001, .0002, 0, .001, 0, 0)),
            ((1, 0, 0, 1, 0, 0), (.001, 0, 0, .001, 0, 0), (.001, 0, 0, .001, 0, 0)),
            ((.001, 0, 0, .001, 0, 0), (2, 0, 0, 2, 10, 20), (.002, 0, 0, .002, .01, .02)),
            ((.001, 0, .0002, .001, .1, .2), (1, .25, -.125, 1, 100, 200),
             (.00105, .00025, .000075, .001, .24, .4)),
        ]
        for aa, hint, (top_matrix, subsidiary, effective) in itertools.product((0, 1), (0, 1), matrix_cases):
            content = 'BT /F 18 Tf 20.1875 30.1875 Td <0002> Tj ET'
            actual = render(content, aa=aa, hint=hint, font=cid_matrix_font(top_matrix, subsidiary), cid=True, metrics='/DW 600')
            expected = render(content, aa=aa, hint=hint, font=cid_matrix_font(effective), cid=True, metrics='/DW 600')
            assert actual == expected, ('CID FontMatrix', hint, top_matrix, subsidiary)
            assert any(value != 255 for value in actual), 'CID FontMatrix produced a blank glyph'
        # Other formats and invisible/collapsed text keep their existing paths.
        from test_pdf_embedded_fonts import fixture as tt_pdf
        from test_pdf_type1_render import fixture as type1_pdf
        for maker in (tt_pdf, type1_pdf):
            path.write_bytes(maker('BT /F 18 Tf 20 30 Td (A) Tj ET'))
            results = [subprocess.check_output([probe, str(path), '0', '1', '0', str(hint)])
                       for hint in (0, 1)]
            assert results[0] == results[1]
            checks += 2
        from test_pdf_cff import fixture as cff_font
        from test_pdf_cff_hints import entry
        from test_pdf_type2 import command
        bad = cff_font(programs=[b'\x0e', b'\x0e', command([0, 0], 21) + command([400, 0, 0, 600], 5) + b'\x0e', b'\x0e'],
                       private_prefixes=[entry(0, [1])])
        path.write_bytes(pdf('BT /F 18 Tf 20 30 Td (A) Tj ET', font=bad))
        result = subprocess.run([probe, str(path), '0', '1', '0', '1'], capture_output=True)
        assert result.returncode == 3 and not result.stdout
        allocation_check(fail=1)
        for state in ('/F 0 Tf', '/F 18 Tf 0 Tz', '/F 18 Tf 3 Tr'):
            render(f'BT {state} 20 30 Td (A) Tj ET', font=bad)
        for option in (-1, 2):
            result = subprocess.run([probe, str(path), '0', '1', '0', str(option)], capture_output=True)
            assert result.returncode == 3 and not result.stdout
            checks += 1
        from test_pdf_forms import form
        from test_pdf_type3 import stream
        local_font = b'<< /Type /Font /Subtype /Type1 /FontDescriptor 13 0 R /Encoding << /Differences [66 /Aacute] >> /FirstChar 66 /LastChar 66 /Widths [700] >>'
        descriptor = b'<< /Type /FontDescriptor /FontName /CupidShapes /Flags 32 /FontBBox [0 -200 1000 1000] /Ascent 800 /Descent -200 /CapHeight 700 /ItalicAngle 0 /StemV 80 /FontFile3 14 0 R >>'
        page = b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R /H 12 0 R >> /XObject << /G 20 0 R >> >> >>'
        content = 'BT /F 18 Tf 60 30 Td (B) Tj ET'
        objects = {3: page, 12: local_font, 13: descriptor, 14: stream(FONT, '/Subtype /Type1C'),
                   20: form(content, '/BBox [0 0 120 90] /Resources << /Font << /F 12 0 R >> >>')}
        for aa in (0, 1):
            assert render('/G Do', aa, extra=objects) == render(content.replace('/F ', '/H '), aa, extra=objects)
            path.write_bytes(pdf('/G Do', extra=objects))
            allocation_check(aa)
        from test_pdf_standard_fonts import fixture as standard_pdf
        from test_pdf_standard_marks import embedded_font
        from generate_pdf_standard_metrics import metrics as afm_metrics, SOURCE as afms
        source = Path('third_party/standard-spacing-marks')
        for row in json.loads((source / 'manifest.json').read_text())['fonts']:
            name = row['standard_name']
            data = (source / row['file']).read_bytes()
            width = afm_metrics(afms / (name + '.afm'))[0]['commaaccent']
            for mode in (0, 2, 7):
                content = f'.75 w BT /F 30 Tf {mode} Tr 20 55 Td (AAA) Tj ET'
                if mode == 7:
                    content += ' 1 0 0 rg 0 0 120 90 re f'
                outputs = []
                for extra in (None, embedded_font(data, width)):
                    path.write_bytes(standard_pdf(content, name, '/Encoding << /Differences [65 /commaaccent] >>', extra=extra))
                    outputs.append(subprocess.check_output([probe, str(path), '0', '1', '0', '1']))
                    checks += 1
                assert outputs[0] == outputs[1], (name, mode, 'hinted fallback changed glyph or PDF width')
            allocation_check()
        for name in ('empty-mask-recovery', 'empty-counter-recovery', 'mask-padding-recovery'):
            render('BT /F 18 Tf 20 30 Td (A) Tj ET', font=FONTS_AUTHORED[name],
                   encoding='<< /Differences [65 /space] >>')
        if len(sys.argv) > 3:
            from test_sixel import decode
            binary = str(Path(sys.argv[3]).resolve())
            expected = render('1 0 0 rg BT /F 18 Tf 20.1875 30.1875 Td (AB) Tj ET')
            want = [tuple(expected[i+c]*100//255 for c in range(3)) for i in range(0, len(expected), 4)]
            for name in ('case.pdf', 'case.PdF', 'extensionless'):
                target = Path(directory) / name
                target.write_bytes(path.read_bytes())
                result = subprocess.run([binary, '--sixel', '--sixel-dither', 'none', '--pdf-text-aa',
                                         '--pdf-cff-hinting', str(target), '160', '120'],
                                        capture_output=True, timeout=20)
                assert result.returncode == 0, result.stderr
                transparent, dimensions, pixels = decode(result.stdout)
                assert not transparent and dimensions == (160, 120) and pixels == want
                checks += 1
        try:
            import freetype
            from test_pdf_coverage_reference import reference_coverage
        except (ImportError, SystemExit):
            freetype = None
        if freetype:
            font_path = Path(directory) / 'font.cff'

            def reference(face, gid, size, tm, tx, ty):
                a, b, c, d = tm
                ppem = max(1, math.floor(math.hypot(c, d) * size * RATIO + .5))
                fixed = [math.trunc(v * size * RATIO / ppem * 65536) for v in (a, b, c, d)]
                ox = round((tx + 1.125) * RATIO * 64)
                oy = round((90 - ty) * RATIO * 64)
                face.set_pixel_sizes(ppem, ppem)
                face.set_transform(freetype.Matrix(fixed[0], fixed[2], fixed[1], fixed[3]),
                                   freetype.Vector(ox, 120 * 64 - oy))
                face.load_glyph(gid, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT)
                outline = face.glyph.outline
                ends = set(outline.contours)
                points = [(x, y, tag & 3, int(i in ends)) for i, ((x, y), tag) in
                          enumerate(zip(outline.points, outline.tags))]
                mask = reference_coverage(points, 160, 120)
                expected = bytearray(b'\xff' * (160 * 120 * 4))
                for y in range(120):
                    for x in range(160):
                        coverage = mask[(119 - y) * 160 + x]
                        at = (y * 160 + x) * 4
                        expected[at + 1] = expected[at + 2] = 255 - coverage
                return expected

            matrices = ((1, 0, 0, 1), (1.5, 0, 0, .75), (0, 1, -1, 0),
                        (-1, 0, 0, 1), (.75, .25, -.125, 1))
            font_path.write_bytes(FONT)
            for char, gid, size, tm in itertools.product(('A', 'B'), (2,), (12, 18, 30), matrices):
                gid = 2 if char == 'A' else 4
                face = freetype.Face(str(font_path))
                expected = reference(face, gid, size, tm, 40.1875, 35.1875)
                content = f'1 0 0 rg BT /F {size} Tf ' + ' '.join(map(str, (*tm, 40.1875, 35.1875))) + f' Tm ({char}) Tj ET'
                actual = render(content)
                assert actual == expected, (char, size, tm, sum(a != b for a, b in zip(actual, expected)))
                references += 1
            for char, gid in (('A', 2), ('B', 4)):
                face = freetype.Face(str(font_path))
                reference(face, gid, 18, matrices[0], 20.1875, 30.1875)
                outline = face.glyph.outline
                commands = []
                first = 0
                for last in outline.contours:
                    contour = outline.points[first:last + 1]
                    assert all((tag & 3) == 1 for tag in outline.tags[first:last + 1])
                    for i, (x, y) in enumerate(contour):
                        px = x / 64 / RATIO - 1.125
                        py = 90 - (120 - y / 64) / RATIO
                        commands.append(f'{px:.12g} {py:.12g} ' + ('m' if i == 0 else 'l'))
                    commands.append('h')
                    first = last + 1
                geometry = ' '.join(commands)
                glyph = f'BT /F 18 Tf MODE Tr 20.1875 30.1875 Td ({char}) Tj ET'
                for style in ('1 w', '1 w 1 J 1 j', '1 w 2 J 2 j [2 1] .5 d'):
                    assert render(style + ' ' + glyph.replace('MODE', '1')) == render(style + ' ' + geometry + ' S'), 'hinted stroke device outline'
                paint = '1 0 0 rg 0 0 120 90 re f'
                assert render(glyph.replace('MODE', '7') + ' ' + paint) == render(geometry + ' W n ' + paint), 'hinted clip device outline'
            for name, stem in FONTS.items():
                data = font_bytes(stem)
                font_path.write_bytes(data)
                for char, size, tm in itertools.product(('A', 'g'), (12, 24), matrices):
                    face = freetype.Face(str(font_path))
                    gid = face.get_char_index(ord(char))
                    if not gid:
                        continue
                    expected = reference(face, gid, size, tm, 40.1875, 35.1875)
                    actual = render(f'1 0 0 rg BT /F {size} Tf ' + ' '.join(map(str, (*tm, 40.1875, 35.1875))) + f' Tm ({char}) Tj ET',
                                    font=data, encoding='/WinAnsiEncoding',
                                    metrics=f'/FirstChar {ord(char)} /LastChar {ord(char)} /Widths [600]')
                    assert actual == expected, (name, char, size, tm, sum(a != b for a, b in zip(actual, expected)))
                    references += 1
            for mark in sorted(source.glob('*.cff')):
                data = mark.read_bytes()
                font_path.write_bytes(data)
                for size, tm in itertools.product((12, 24), matrices):
                    face = freetype.Face(str(font_path))
                    expected = reference(face, 1, size, tm, 40.1875, 35.1875)
                    actual = render(f'1 0 0 rg BT /F {size} Tf ' + ' '.join(map(str, (*tm, 40.1875, 35.1875))) + ' Tm (A) Tj ET',
                                    font=data, encoding='<< /Differences [65 /space_uni0326] >>')
                    assert actual == expected, (mark.name, size, tm)
                    references += 1
    print(f'CFF page hinting: {checks} page/API/CLI checks; {allocations} allocation/cache checks; {references} independent grayscale pages')


if __name__ == '__main__':
    main()
