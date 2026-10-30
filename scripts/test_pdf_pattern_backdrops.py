#!/usr/bin/env python3
"""Verify pattern evaluation against the actual painted object's backdrop."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_groups import fixture
from test_pdf_forms import form
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = refs = 0
    background = '1 0 0 rg 0 0 9 12 re f 0 1 0 rg 9 0 9 12 re f '
    with tempfile.TemporaryDirectory(prefix='cupidimage-pattern-backdrops-') as tmp:
        path = Path(tmp) / 'case.pdf'

        def check(name, pdf, expected=None, reject=False):
            nonlocal checks, refs
            path.write_bytes(pdf)
            if reject:
                run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
                assert run.returncode == 3, (name, run.stderr)
                checks += 1
                return
            want = bytes(v for y in range(16) for x in range(24) for v in (*expected(x, y), 255))
            if '--reference-only' in sys.argv:
                actual_rgba = want
            else:
                run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
                assert run.returncode == 0, (name, run.stderr)
                assert struct.unpack('=III', run.stdout[:12]) == (24, 16, 1)
                assert run.stdout[12:] == want, (name, next(
                    (i, a, b) for i, (a, b) in enumerate(zip(run.stdout[12:], want)) if a != b))
                actual_rgba = run.stdout[12:]
                checks += 1
            if '--reference' in sys.argv or '--reference-only' in sys.argv:
                prefix = Path(tmp) / 'reference'
                ref = subprocess.run(['pdftoppm', '-r', '96', '-aa', 'no', '-aaVector', 'no',
                                      '-singlefile', str(path), str(prefix)], capture_output=True, timeout=30)
                assert ref.returncode == 0, ref.stderr
                w, h, rgb = read_ppm(prefix.with_suffix('.ppm'))
                actual = bytes(v for i, v in enumerate(actual_rgba) if i % 4 != 3)
                assert (w, h) == (24, 16) and max(abs(a-b) for a,b in zip(actual,rgb)) <= 1, (name, max(abs(a-b) for a,b in zip(actual,rgb)), tuple(rgb[:3]), tuple(actual[:3]))
                refs += 1

        for colored in (False, True):
            pattern = stream(('.5 g ' if colored else '') + '/Blend gs 0 0 3 3 re f',
                             '/Type /Pattern /PatternType 1 /PaintType ' + ('1' if colored else '2') +
                             ' /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 '
                             '/Resources << /ExtGState << /Blend << /BM /Multiply >> >> >>')
            resources = '/Resources << /Pattern << /P 6 0 R >> /ColorSpace << /Pcs [/Pattern /DeviceGray] >> /ExtGState << /Half << /ca .5 >> >> >>'
            select = '/Pattern cs /P scn ' if colored else '/Pcs cs .5 /P scn '
            for alpha in (1, .5):
                for repetitions in (1, 2):
                    content = background + ('/Half gs ' if alpha == .5 else '') + select
                    content += '0 0 18 12 re f ' * repetitions
                    channel = int(255 * (1 - .5 * alpha) ** repetitions + .5)
                    check(f'multiply-{colored}-{alpha}-{repetitions}',
                          fixture(content, attrs=resources, extra={6: pattern}),
                          lambda x, y: (channel, 0, 0) if x < 12 else (0, channel, 0))
        for mode, left, right in (
                ('Screen', (255, 128, 128), (128, 255, 128)),
                ('Overlay', (255, 0, 0), (0, 255, 0)),
                ('Darken', (128, 0, 0), (0, 128, 0)),
                ('Lighten', (255, 128, 128), (128, 255, 128)),
                ('ColorDodge', (255, 0, 0), (0, 255, 0)),
                ('ColorBurn', (255, 0, 0), (0, 255, 0)),
                ('HardLight', (255, 0, 0), (0, 255, 0)),
                ('SoftLight', (255, 0, 0), (0, 255, 0)),
                ('Difference', (128, 128, 128), (128, 128, 128)),
                ('Exclusion', (128, 128, 128), (128, 128, 128)),
                ('Hue', (77, 77, 77), (150, 150, 150)),
                ('Saturation', (77, 77, 77), (150, 150, 150)),
                ('Color', (77, 77, 77), (150, 150, 150))):
            pattern = stream('.5 g /B gs 0 0 3 3 re f',
                             '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 '
                             '/BBox [0 0 3 3] /XStep 3 /YStep 3 '
                             '/Resources << /ExtGState << /B << /BM /' + mode + ' >> >> >>')
            check('blend-' + mode,
                  fixture(background + '/Pattern cs /P scn 0 0 18 12 re f',
                          attrs='/Resources << /Pattern << /P 6 0 R >> >>', extra={6: pattern}),
                  lambda x, y: left if x < 12 else right)
        inner = form('.5 g /B gs 0 0 3 3 re f',
                     '/BBox [0 0 3 3] /Resources << /ExtGState << /B << /BM /Multiply >> >> >>')
        pattern = stream('/F Do', '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 '
                         '/BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << /XObject << /F 7 0 R >> >>')
        for clipped in (False, True):
            content = background + ('0 0 9 12 re W n ' if clipped else '')
            content += '/Pattern cs /P scn 0 0 18 12 re f'
            check('nested-form-' + str(clipped),
                  fixture(content, attrs='/Resources << /Pattern << /P 6 0 R >> >>',
                          extra={6: pattern, 7: inner}),
                  lambda x, y: (128, 0, 0) if x < 12 else (0, 255 if clipped else 128, 0))
        pattern = stream('.5 g /B gs 0 0 3 3 re f',
                         '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 '
                         '/BBox [0 0 3 3] /XStep 3 /YStep 3 '
                         '/Resources << /ExtGState << /B << /BM /Multiply >> >> >>')
        check('stroke', fixture(background + '/Pattern CS /P SCN 12 w 0 6 m 18 6 l S',
                               attrs='/Resources << /Pattern << /P 6 0 R >> >>', extra={6: pattern}),
              lambda x, y: (128, 0, 0) if x < 12 else (0, 128, 0))
        inline = b'BI /W 1 /H 1 /BPC 1 /IM true ID \x00 EI '
        check('consecutive-inline-stencils',
              fixture((background + '/Pattern cs /P scn 18 0 0 12 0 0 cm ').encode() + inline + inline,
                      attrs='/Resources << /Pattern << /P 6 0 R >> >>', extra={6: pattern}),
              lambda x, y: (64, 0, 0) if x < 12 else (0, 64, 0))
        check('combined-fill-stroke',
              fixture(background + '/Pattern cs /P scn /Pattern CS /P SCN 3 w 0 0 18 12 re B',
                      attrs='/Resources << /Pattern << /P 6 0 R >> >>', extra={6: pattern}),
              lambda x, y: (128, 0, 0) if x < 12 else (0, 128, 0))
        font = (b'<< /Type /Font /Subtype /Type3 /FontBBox [0 0 18 12] '
                b'/FontMatrix [1 0 0 1 0 0] /FirstChar 65 /LastChar 65 /Widths [18] '
                b'/Encoding << /Differences [65 /A] >> /CharProcs << /A 8 0 R >> >>')
        for knockout in (False, True):
            check('overlapping-text-' + str(knockout),
                  fixture(background + '/Pattern cs /P scn /T gs BT /F 1 Tf -18 Tc (AA) Tj ET',
                          attrs='/Resources << /Pattern << /P 6 0 R >> /Font << /F 7 0 R >> '
                                '/ExtGState << /T << /TK ' + str(knockout).lower() + ' >> >> >>',
                          extra={6: pattern, 7: font, 8: stream('18 0 0 0 18 12 d1 0 0 18 12 re f')}),
                  lambda x, y: ((128 if knockout else 64), 0, 0) if x < 12 else
                               (0, (128 if knockout else 64), 0))
        mask = form('.5 g 0 0 18 12 re f',
                    '/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS /DeviceGray >>')
        check('caller-soft-mask',
              fixture(background + '/M gs /Pattern cs /P scn 0 0 18 12 re f',
                      attrs='/Resources << /Pattern << /P 6 0 R >> /ExtGState << /M << '
                            '/SMask << /S /Luminosity /G 7 0 R >> >> >> >>',
                      extra={6: pattern, 7: mask}),
              lambda x, y: (191, 0, 0) if x < 12 else (0, 191, 0))
        cmyk = stream('.8 0 0 .6 k /B gs 0 0 3 3 re f',
                      '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 '
                      '/BBox [0 0 3 3] /XStep 3 /YStep 3 '
                      '/Resources << /ExtGState << /B << /BM /Multiply >> >> >>')
        check('native-cmyk-gray',
              fixture(background + '/Pattern cs /P scn 0 0 18 12 re f',
                      group='<< /S /Transparency /I true /CS /DeviceGray >>',
                      attrs='/Resources << /Pattern << /P 6 0 R >> >>', extra={6: cmyk}),
              lambda x, y: (12, 12, 12) if x < 12 else (24, 24, 24))
        for inner_alpha in (1, .5):
            for outer_alpha in (1, .5):
                pattern = stream('.5 g /B gs 0 0 6 3 re f',
                                 '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 '
                                 '/BBox [0 0 6 3] /XStep 3 /YStep 3 '
                                 '/Resources << /ExtGState << /B << /BM /Multiply /ca ' +
                                 str(inner_alpha) + ' >> >> >>')
                content = background + '/O gs /Pattern cs /P scn 0 0 18 12 re f'
                channel = int(255 * ((1 - outer_alpha) + outer_alpha * (1 - .5 * inner_alpha)**2) + .5)
                check(f'overlapping-tiles-{inner_alpha}-{outer_alpha}',
                      fixture(content, attrs='/Resources << /Pattern << /P 6 0 R >> '
                              '/ExtGState << /O << /ca ' + str(outer_alpha) + ' >> >> >>',
                              extra={6: pattern}),
                      lambda x, y: (channel, 0, 0) if x < 12 else (0, channel, 0))
        for program in ('/B gs BT', '/B gs 1 0 0 rg 0 0 3 3 re f'):
            pattern = stream(program, '/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 '
                             '/BBox [0 0 3 3] /XStep 3 /YStep 3 '
                             '/Resources << /ExtGState << /B << /BM /Multiply >> >> >>')
            check('deferred-validation-' + program,
                  fixture('/Pcs cs .5 /P scn 0 0 18 12 re f',
                          attrs='/Resources << /Pattern << /P 6 0 R >> '
                                '/ColorSpace << /Pcs [/Pattern /DeviceGray] >> >>', extra={6: pattern}),
                  reject=True)
        recursive = stream('/B gs /Pattern cs /P scn 0 0 3 3 re f',
                           '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 '
                           '/BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << '
                           '/ExtGState << /B << /BM /Multiply >> >> /Pattern << /P 6 0 R >> >>')
        check('recursive-deferred-definition',
              fixture('/Pattern cs /P scn 0 0 18 12 re f',
                      attrs='/Resources << /Pattern << /P 6 0 R >> >>', extra={6: recursive}), reject=True)
        for matrix in ((1, 0, 0, 1, 0, 0), (1, 0, 0, 1, .75, .75),
                       (-1, 0, 0, 1, 18, 0), (0, 1, -1, 0, 18, 0),
                       (1.5, 0, 0, 1, 0, 0), (1, 0, .75, 1, 0, 0)):
            a, b, c, d, e, f = matrix
            det = a * d - b * c
            def expected(x, y):
                wx, wy = (x + .5) * .75 - e, 12 - (y + .5) * .75 - f
                u, v = (d * wx - c * wy) / det, (-b * wx + a * wy) / det
                channel = 128 if u % 3 < 1.5 and v % 3 < 1.5 else 255
                return (channel, 0, 0) if x < 12 else (0, channel, 0)
            pattern = stream('.5 g /B gs 0 0 1.5 1.5 re f',
                             '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 '
                             '/BBox [0 0 1.5 1.5] /XStep 3 /YStep 3 /Matrix [' +
                             ' '.join(map(str, matrix)) + '] '
                             '/Resources << /ExtGState << /B << /BM /Multiply >> >> >>')
            check('matrix-' + str(matrix),
                  fixture(background + '/Pattern cs /P scn 0 0 18 12 re f',
                          attrs='/Resources << /Pattern << /P 6 0 R >> >>', extra={6: pattern}), expected)
    print(f'PDF pattern backdrops: {checks} checks passed; {refs} independent comparisons')


if __name__ == '__main__':
    main()
