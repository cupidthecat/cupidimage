#!/usr/bin/env python3
"""Check native pattern-cache precision and destination blending-space changes."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_type3 import stream
from test_pdf_forms import form
from test_pdf_object_syntax import pdf as object_pdf


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-pattern-colors-') as tmp:
        path = Path(tmp) / 'case.pdf'

        def check(name, content, pattern, expected, extra=None, resources='', packed=False):
            nonlocal checks
            objects = {5: pattern}
            objects.update(extra or {})
            page = ('/MediaBox [0 0 18 12] /Resources << /Pattern << /P ' +
                    ('8' if packed else '5') + ' 0 R >> /XObject << /F 6 0 R >> '
                    '/ExtGState << /A << /ca .75 >> /Shape << /AIS true >> >> ' + resources + ' >>')
            if packed:
                objects[1] = b'<< /Type /Catalog /Pages 2 0 R >>'
                objects[2] = b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>'
                objects[3] = ('<< /Type /Page /Parent 2 0 R /Contents 4 0 R ' + page + ' >>').encode()
                objects[4] = stream(content)
                objects[5] = stream(zlib.compress(b'8 0 ' + pattern),
                                    '/Type /ObjStm /N 1 /First 4 /Filter /FlateDecode')
                pdf = object_pdf(objects, compressed=[8])
            else:
                pdf = document(page, stream=content, extra=objects)
            path.write_bytes(pdf)
            run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=20)
            assert run.returncode == 0, (name, run.stderr)
            assert struct.unpack('=III', run.stdout[:12]) == (24, 16, 1)
            want = bytes(v for y in range(16) for x in range(24) for v in (*expected(x, y), 255))
            assert run.stdout[12:] == want, (name, next(
                (i, a, b) for i, (a, b) in enumerate(zip(run.stdout[12:], want)) if a != b))
            checks += 1

        for shading in (False, True):
            if shading:
                pattern = (b'<< /PatternType 2 /Shading << /ShadingType 2 /ColorSpace /DeviceCMYK '
                           b'/Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] '
                           b'/C0 [.8 0 0 .6] /C1 [.8 0 0 .6] /N 1 >> >> >>')
            else:
                pattern = stream('.8 0 0 .6 k 0 0 3 3 re f',
                                 '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 '
                                 '/BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>')
            gray = form('0 0 18 12 re f', '/BBox [0 0 18 12] '
                        '/Group << /S /Transparency /I true /CS /DeviceGray >>')
            check('inherited-gray-' + str(shading), '/Pattern cs /P scn /F Do', pattern,
                  lambda x, y: (41, 41, 41), extra={6: gray})
            gray = form('0 0 18 12 re f', '/BBox [9 0 18 12] '
                        '/Group << /S /Transparency /I true /CS /DeviceGray >>')
            check('rgb-gray-rgb-' + str(shading),
                  '/Pattern cs /P scn 0 0 18 12 re f /F Do 0 0 9 12 re f', pattern,
                  lambda x, y: (0, 102, 102) if x < 12 else (41, 41, 41), extra={6: gray})
            gray = form('/Pattern cs /P scn 0 0 18 12 re f', '/BBox [0 0 18 12] '
                        '/Group << /S /Transparency /I true /CS /DeviceGray >>')
            check('selected-in-gray-' + str(shading), '/F Do', pattern,
                  lambda x, y: (41, 41, 41), extra={6: gray})
            gray = form('0 0 18 12 re f', '/BBox [9 0 18 12] '
                        '/Group << /S /Transparency /I true /CS /DeviceGray >>')
            check('reselected-after-gray-' + str(shading),
                  '/Pattern cs /P scn /F Do /P scn 0 0 9 12 re f', pattern,
                  lambda x, y: (0, 102, 102) if x < 12 else (41, 41, 41), extra={6: gray})
            if shading:
                check('compressed-pattern-definition',
                      '/Pattern cs /P scn /F Do 0 0 9 12 re f', pattern,
                      lambda x, y: (0, 102, 102) if x < 12 else (41, 41, 41),
                      extra={6: gray}, packed=True)
        pattern = stream('/A gs .003 g 0 0 3 3 re f 0 0 3 3 re f',
                         '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 '
                         '/BBox [0 0 3 3] /XStep 3 /YStep 3 '
                         '/Resources << /ExtGState << /A << /ca .5 >> >> >>')
        check('cell-precision', '/Pattern cs /P scn 0 0 18 12 re f', pattern, lambda x, y: (64, 64, 64))
        pattern = (b'<< /PatternType 2 /Shading << /ShadingType 2 /ColorSpace /DeviceGray '
                   b'/Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] '
                   b'/C0 [.003] /C1 [.003] /N 1 >> >> >>')
        check('shading-precision', '/A gs /Pattern cs /P scn 0 0 18 12 re f', pattern,
              lambda x, y: (64, 64, 64))
        for value in (.001, .003, .011, .499, .501, .997):
            for alpha in (.25, .5, .75):
                cell = stream(f'{value} g 0 0 3 3 re f',
                              '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 '
                              '/BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>')
                painting = form('/Pattern cs /P scn /Opacity gs 0 0 18 12 re f',
                                '/BBox [0 0 18 12] /Resources << /Pattern << /P 5 0 R >> '
                                '/ExtGState << /Opacity << /ca ' + str(alpha) + ' >> >> >>')
                expected = int(255 * (alpha * value + 1 - alpha) + .5)
                check(f'precision-{value}-{alpha}', '/F Do', cell,
                      lambda x, y: (expected,) * 3, extra={6: painting})
        mask = form('.5 g 0 0 18 12 re f',
                    '/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS /DeviceGray >>')
        pattern = (b'<< /PatternType 2 /ExtGState << /SMask << /S /Luminosity /G 7 0 R >> >> '
                   b'/Shading << /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] '
                   b'/Function << /FunctionType 2 /Domain [0 1] /C0 [0 0 1] /C1 [0 0 1] /N 1 >> >> >>')
        group = form('1 0 0 rg 0 0 18 12 re f /Pattern cs /P scn 0 0 18 12 re f',
                     '/BBox [0 0 18 12] /Group << /S /Transparency /I true /K true >> '
                     '/Resources << /Pattern << /P 5 0 R >> >>')
        check('initial-shape-cache-key', '/Pattern cs /P scn /Shape gs /F Do', pattern,
              lambda x, y: (128, 0, 128), extra={6: group, 7: mask})
    print(f'PDF pattern colors: {checks} analytic checks passed')


if __name__ == '__main__':
    main()
