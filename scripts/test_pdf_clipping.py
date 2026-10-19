#!/usr/bin/env python3
"""Check PDF clipping against complete pixel grids and unclipped paint controls."""
import pathlib
import struct
import subprocess
import sys
import tempfile

from test_pdf_pages import document
from test_pdf_forms import form
from test_pdf_images import image_stream

GRAY, RED, BLUE = (128, 128, 128), (255, 0, 0), (0, 0, 255)
FILL = '-100 -100 300 300 re f'
LEFT = '0 0 9 12 re'
QUARTER = '0 0 9 6 re'
RING = '0 0 18 12 re 4.5 3 9 6 re'


def main():
    probe = str(pathlib.Path(sys.argv[1]).resolve())
    failures = count = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-pdf-clipping-') as directory:
        def check(name, content, expected=None, same=None, reject=False, extra=None,
                  attrs='', dimensions=(24, 16), box='0 0 18 12'):
            nonlocal failures, count
            count += 1
            path = pathlib.Path(directory) / f'{count}-{name}.pdf'
            path.write_bytes(document(f'/MediaBox [{box}] {attrs}',
                                      stream='0.5 g -100 -100 300 300 re f ' + content,
                                      extra=extra))
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=15)
            try:
                if reject:
                    assert result.returncode == 3, (result.returncode, result.stderr[:1000])
                    assert result.stderr
                    return
                assert result.returncode == 0, result.stderr[:1000]
                width, height = dimensions
                assert struct.unpack('=III', result.stdout[:12]) == (width, height, 1)
                pixels = result.stdout[12:]
                assert len(pixels) == width * height * 4
                if same is not None:
                    assert pixels == same
                if expected is not None:
                    for y in range(height):
                        for x in range(width):
                            got = tuple(pixels[(y * width + x) * 4:(y * width + x + 1) * 4])
                            want = (*expected(x, y), 255)
                            assert got == want, (x, y, got, want)
                return pixels
            except (AssertionError, struct.error) as error:
                failures += 1
                print('FAIL', name, str(error), file=sys.stderr)

        quarter = lambda x, y: RED if x < 12 and y >= 8 else GRAY
        left = lambda x, y: RED if x < 12 else GRAY
        for operator in ('W', 'W*'):
            check('rectangle-' + operator, QUARTER + f' {operator} n 1 0 0 rg ' + FILL, quarter)
            check('clip-does-not-paint-' + operator, QUARTER + f' {operator} n', lambda x, y: GRAY)
        for path in ('', '0 0 0 12 re', '100 100 9 6 re', '0 0 m 18 12 l'):
            check('empty-clip', path + ' W n 1 0 0 rg ' + FILL, lambda x, y: GRAY)
        check('implicit-triangle', '0 0 m 18 0 l 0 12 l W n 1 0 0 rg ' + FILL,
              lambda x, y: RED if (x + 0.5) / 24 + (15.5 - y) / 16 < 1 else GRAY)
        check('nonzero-overlap', RING + ' W n 1 0 0 rg ' + FILL, lambda x, y: RED)
        hole = lambda x, y: GRAY if 6 <= x < 18 and 4 <= y < 12 else RED
        check('even-odd-hole', RING + ' W* n 1 0 0 rg ' + FILL, hole)
        check('opposite-winding-hole', '0 0 18 12 re 4.5 3 m 4.5 9 l 13.5 9 l 13.5 3 l h W n 1 0 0 rg ' + FILL, hole)
        check('intersect-clips', LEFT + ' W n 0 0 18 6 re W n 1 0 0 rg ' + FILL, quarter)
        check('disjoint-clips', LEFT + ' W n 9 0 9 12 re W n 1 0 0 rg ' + FILL, lambda x, y: GRAY)
        check('restore-unclipped', 'q ' + LEFT + ' W n 1 0 0 rg ' + FILL + ' Q 0 0 1 rg 9 0 9 12 re f',
              lambda x, y: RED if x < 12 else BLUE)
        check('restore-parent-clip', LEFT + ' W n q 0 0 18 6 re W n 1 0 0 rg ' + FILL + ' Q 0 0 1 rg 0 6 18 6 re f',
              lambda x, y: (RED if y >= 8 else BLUE) if x < 12 else GRAY)
        check('unbalanced-save-cleanup', QUARTER + ' W n q q 1 0 0 rg ' + FILL, quarter)
        check('path-survives-restore', 'q 2 0 0 2 0 0 cm 0 0 4.5 3 re Q W n 1 0 0 rg ' + FILL, quarter)
        check('path-survives-cm', QUARTER + ' 1 0 0 1 9 6 cm W n 1 0 0 rg ' + FILL, quarter)
        check('rotated-path', 'q 0 1 -1 0 18 0 cm 0 0 9 6 re Q W n 1 0 0 rg ' + FILL,
              lambda x, y: RED if x >= 16 and y >= 4 else GRAY)
        check('sheared-path', 'q 1 0 1 1 0 0 cm ' + QUARTER + ' Q W n 1 0 0 rg ' + FILL,
              lambda x, y: RED if y >= 8 and 0 <= (x + y - 15) * 0.75 < 9 else GRAY)

        # Painting uses the old clip; W takes effect after the path ends.
        for end in ('S', 's', 'f', 'F', 'f*', 'B', 'B*', 'b', 'b*'):
            prefix = '1 0 0 rg 0 0 1 RG 3 w ' + QUARTER
            reference = check('paint-control-' + end, prefix + ' ' + end)
            check('deferred-clip-' + end, prefix + ' W ' + end, same=reference)
        for curve in ('0 0 m 0 12 18 12 18 0 c', '0 0 m 18 12 18 0 v', '0 0 m 0 12 18 0 y'):
            reference = check('curve-control', '1 0 0 rg ' + curve + ' f')
            check('curve-clip', curve + ' W n 1 0 0 rg ' + FILL, same=reference)
        check('clipped-stroke', LEFT + ' W n 1 0 0 RG 30 w 0 6 m 18 6 l S', left)

        image = image_stream(bytes(RED), '/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB')
        check('clipped-image', QUARTER + ' W n 18 0 0 12 0 0 cm /Im0 Do', quarter,
              attrs='/Resources << /XObject << /Im0 5 0 R >> >>', extra={5: image})
        text = '1 0 0 rg BT /F1 6 Tf 1 0 0 1 0 6 Tm (AAA) Tj ET'
        reference = check('text-control', text)
        if reference is not None:
            check('clipped-text', LEFT + ' W n ' + text,
                  same=b''.join(reference[p:p + 4] if p // 4 % 24 < 12 else bytes((*GRAY, 255))
                                for p in range(0, len(reference), 4)))
        resources = '/Resources << /XObject << /F0 5 0 R >> >>'
        child = form('0 0 18 6 re W n 1 0 0 rg ' + FILL, '/BBox [0 0 18 12]')
        check('form-clip-isolation', LEFT + ' W n /F0 Do 0 0 1 rg 0 6 18 6 re f',
              lambda x, y: (RED if y >= 8 else BLUE) if x < 12 else GRAY,
              attrs=resources, extra={5: child})
        check('form-bbox-intersection', LEFT + ' W n /F0 Do', quarter,
              attrs=resources, extra={5: form('1 0 0 rg ' + FILL)})
        check('offset-page', '30 60 9 6 re W n 1 0 0 rg ' + FILL, quarter, box='30 60 48 72')
        check('rotated-page', QUARTER + ' W n 1 0 0 rg ' + FILL,
              lambda x, y: RED if x < 8 and y < 12 else GRAY,
              attrs='/Rotate 90', dimensions=(16, 24))
        check('maximum-save-depth', 'q ' * 32 + LEFT + ' W n 1 0 0 rg ' + FILL + ' Q' * 32, left)
        check('save-overflow', 'q ' * 33, reject=True)
        check('path-overflow', '0 0 m ' + '0 1 l 1 0 l ' * 2050 + 'W n', reject=True)
        check('clip-count-limit', 'W n ' * 4097, reject=True)
        check('clip-work-limit', '0 0 600 600 re ' * 500 + 'W n', box='0 0 600 600', reject=True)
        check('malformed-after-clip', LEFT + ' W n q (unterminated', reject=True)
        check('recursive-form-after-clip', LEFT + ' W n /F0 Do', reject=True,
              attrs=resources, extra={5: form('/F0 Do')})
    print(f'{count} PDF clipping checks, {failures} failures')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
