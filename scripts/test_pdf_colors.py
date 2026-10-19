#!/usr/bin/env python3
"""Check device color operators, resource aliases, and graphics state scoping."""
import pathlib
import struct
import subprocess
import sys
import tempfile

from test_pdf_pages import document
from test_pdf_forms import form

FILL = '0 0 18 12 re f'
STROKE = '30 w 0 6 m 18 6 l S'


def main():
    probe = str(pathlib.Path(sys.argv[1]).resolve())
    count = failures = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-pdf-colors-') as directory:
        def check(name, content, color=None, expected=None, reject=False, attrs='', parent='', extra=None):
            nonlocal count, failures
            count += 1
            path = pathlib.Path(directory) / f'{count}-{name}.pdf'
            path.write_bytes(document('/MediaBox [0 0 18 12] ' + attrs, parent,
                                      stream='0.5 g ' + FILL + ' ' + content, extra=extra))
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=15)
            try:
                if reject:
                    assert result.returncode == 3, (result.returncode, result.stderr[:1000])
                    assert result.stderr
                    return
                assert result.returncode == 0, result.stderr[:1000]
                assert struct.unpack('=III', result.stdout[:12]) == (24, 16, 1)
                assert len(result.stdout) == 12 + 24 * 16 * 4
                for y in range(16):
                    for x in range(24):
                        got = tuple(result.stdout[12 + (y * 24 + x) * 4:16 + (y * 24 + x) * 4])
                        want = (*(expected(x, y) if expected else color), 255)
                        assert got == want, (x, y, got, want)
            except (AssertionError, struct.error) as error:
                failures += 1
                print('FAIL', name, str(error), file=sys.stderr)

        colors = [('DeviceGray', '.25', (64, 64, 64)),
                  ('DeviceRGB', '1 0 0', (255, 0, 0)),
                  ('DeviceRGB', '-1 0.5 2', (0, 128, 255)),
                  ('DeviceCMYK', '1 0 0 0', (0, 255, 255)),
                  ('DeviceCMYK', '.25 .5 .75 .25', (128, 64, 0))]
        for space, values, rgb in colors:
            for stroke in (False, True):
                select = 'CS' if stroke else 'cs'
                paint = STROKE if stroke else FILL
                for op in (('SC', 'SCN') if stroke else ('sc', 'scn')):
                    check(space + op, f'/{space} {select} {values} {op} {paint}', rgb)
                check(space + '-reset', f'1 0 0 rg 1 0 0 RG /{space} {select} {paint}', (0, 0, 0))
        check('initial-gray', '.75 sc ' + FILL, (191, 191, 191))
        for direct, values, generic, paint, color in (
            ('0 g', '.25', 'sc', FILL, (64, 64, 64)),
            ('0 G', '.25', 'SCN', STROKE, (64, 64, 64)),
            ('0 0 0 rg', '0 0 1', 'scn', FILL, (0, 0, 255)),
            ('0 0 0 RG', '0 0 1', 'SC', STROKE, (0, 0, 255)),
            ('0 0 0 1 k', '0 1 0 0', 'sc', FILL, (255, 0, 255)),
            ('0 0 0 1 K', '0 1 0 0', 'SCN', STROKE, (255, 0, 255)),
        ):
            check('direct-selects-space', f'{direct} {values} {generic} {paint}', color)
        check('fill-stroke-independent', '/DeviceRGB cs /DeviceGray CS .5 SC 1 0 0 sc ' + FILL, (255, 0, 0))
        check('saved-space', '/DeviceRGB cs q /DeviceGray cs .25 sc 0 0 9 12 re f Q 0 0 1 sc 9 0 9 12 re f',
              expected=lambda x, y: (64, 64, 64) if x < 12 else (0, 0, 255))
        resources = '/Resources << /ColorSpace << /C1 /DeviceRGB >> >>'
        check('page-alias', '/C1 cs 1 0 0 sc ' + FILL, (255, 0, 0), attrs=resources)
        check('inherited-alias', '/C1 cs 1 0 0 sc ' + FILL, (255, 0, 0), parent=resources)
        check('alias-chain', '/C1 cs 1 0 0 sc ' + FILL, (255, 0, 0),
              attrs='/Resources << /ColorSpace << /C1 /C2 /C2 /DeviceRGB >> >>')
        check('indirect-alias', '/C1 cs 1 0 0 sc ' + FILL, (255, 0, 0), attrs='/Resources 5 0 R',
              extra={5: '<< /ColorSpace 6 0 R >>', 6: '<< /C1 7 0 R >>', 7: '/DeviceRGB'})
        check('direct-space-not-resource', '/DeviceRGB cs 1 0 0 sc ' + FILL, (255, 0, 0),
              attrs='/Resources << /ColorSpace << /DeviceRGB /DeviceCMYK >> >>')
        check('page-resource-shadows-parent', '/C1 cs 1 0 0 sc ' + FILL, reject=True,
              attrs='/Resources << >>', parent=resources)
        form_resources = '/Resources << /XObject << /F0 5 0 R >> /ColorSpace << /C1 /DeviceRGB >> >>'
        check('form-inherits-space', '/DeviceRGB cs 1 0 0 sc /F0 Do', (255, 0, 0), attrs=form_resources,
              extra={5: form(FILL, '/BBox [0 0 18 12]')})
        check('form-inherits-alias', '/F0 Do', (255, 0, 0), attrs=form_resources,
              extra={5: form('/C1 cs 1 0 0 sc ' + FILL, '/BBox [0 0 18 12]')})
        check('form-resource-shadow', '/F0 Do', (0, 255, 255), attrs=form_resources,
              extra={5: form('/C1 cs 1 0 0 0 sc ' + FILL,
                     '/BBox [0 0 18 12] /Resources << /ColorSpace << /C1 /DeviceCMYK >> >>')})
        check('form-space-restored', '/DeviceRGB cs /F0 Do 0 0 1 sc ' + FILL, (0, 0, 255), attrs=form_resources,
              extra={5: form('/DeviceGray cs .25 sc ' + FILL, '/BBox [0 0 18 12]')})
        check('empty-form-resources', '/F0 Do', reject=True, attrs=form_resources,
              extra={5: form('/C1 cs 1 0 0 sc ' + FILL, '/BBox [0 0 18 12] /Resources << >>')})
        for content in ('cs', '(DeviceRGB) cs', '/Unknown cs', '/Pattern cs', '/DeviceRGB cs 1 sc',
                        '/DeviceGray cs 1 0 0 sc', '/DeviceCMYK cs 1 0 0 scn', '/DeviceRGB cs /name scn',
                        '/DeviceRGB cs [1 0 0] sc', '1 0 rg', '1 0 0 0 1 k'):
            check('invalid-operands', content + ' ' + FILL, reject=True)
        for aliases, extra in (
            ('/C1 /C1', {}), ('/C1 /C2 /C2 /C1', {}),
            ('/C1 5 0 R', {5: '6 0 R', 6: '5 0 R'}),
            ('/C1 [/DeviceRGB]', {}), ('/C1 /CalRGB', {}),
        ):
            check('invalid-alias', '/C1 cs ' + FILL, reject=True,
                  attrs='/Resources << /ColorSpace << ' + aliases + ' >> >>', extra=extra)
    print(f'{count} PDF color checks, {failures} failures')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
