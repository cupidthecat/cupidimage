#!/usr/bin/env python3
"""Check direct device-space conversion and deferred source quantization."""
from fractions import Fraction
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_groups import fixture
from test_pdf_type3 import stream
from test_pdf_mesh import records as triangle_records
from test_pdf_patch import records as patch_records, control_points


def quantize(value):
    return int(max(0, min(1, value)) * 255 + Fraction(1, 2))


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = 0
    gray_group = '<< /S /Transparency /I true /CS /DeviceGray >>'
    rect = '0 0 18 12 re f'
    with tempfile.TemporaryDirectory(prefix='cupidimage-device-color-') as tmp:
        path = Path(tmp) / 'case.pdf'

        def check(name, pdf, expected):
            nonlocal checks
            path.write_bytes(pdf)
            run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=20)
            assert run.returncode == 0, (name, run.stderr)
            assert struct.unpack('=III', run.stdout[:12]) == (24, 16, 1)
            want = bytes((*expected, 255)) * (24 * 16)
            assert run.stdout[12:] == want, (name, next(
                (i, a, b) for i, (a, b) in enumerate(zip(run.stdout[12:], want)) if a != b))
            checks += 1

        for values in ((.8, .2, .4, .5), (1, 0, 0, .5), (0, 1, 0, .3),
                       (0, 0, 1, .7), (.2, .3, .4, .1), (0, 0, 0, .501),
                       (-1, 2, .4, .2)):
            clipped = [max(0, min(1, c)) for c in values]
            c, m, y, k = clipped
            gray = 1 - min(1, .3 * c + .59 * m + .11 * y + k)
            expected = (quantize(gray),) * 3
            operands = ' '.join(map(str, values))
            check('cmyk-fill-' + operands,
                  fixture(operands + ' k ' + rect, group=gray_group), expected)
            check('cmyk-state-' + operands,
                  fixture(operands + ' k q 1 0 0 rg Q ' + rect, group=gray_group), expected)
            check('cmyk-stroke-' + operands,
                  fixture(operands + ' K 12 w 0 6 m 18 6 l S', group=gray_group), expected)
            shading = ('<< /ShadingType 2 /ColorSpace /DeviceCMYK /Coords [0 0 18 0] '
                       '/Function << /FunctionType 2 /Domain [0 1] /C0 [' + operands +
                       '] /C1 [' + operands + '] /N 1 >> >>').encode()
            check('cmyk-shading-' + operands,
                  fixture('/S sh', group=gray_group,
                          attrs='/Resources << /Shading << /S 6 0 R >> >>', extra={6: shading}), expected)
            radial = shading.replace(b'/ShadingType 2', b'/ShadingType 3').replace(
                b'/Coords [0 0 18 0]', b'/Coords [9 6 0 9 6 30] /Extend [true true]')
            check('cmyk-radial-' + operands,
                  fixture('/S sh', group=gray_group,
                          attrs='/Resources << /Shading << /S 6 0 R >> >>', extra={6: radial}), expected)
            function = stream('{ pop pop ' + ' '.join(map(str, clipped)) + ' }',
                              '/FunctionType 4 /Domain [0 18 0 12] /Range [0 1 0 1 0 1 0 1]')
            plane = b'<< /ShadingType 1 /ColorSpace /DeviceCMYK /Domain [0 18 0 12] /Function 7 0 R >>'
            check('cmyk-function-plane-' + operands,
                  fixture('/S sh', group=gray_group,
                          attrs='/Resources << /Shading << /S 6 0 R >> >>',
                          extra={6: plane, 7: function}), expected)
            image = stream(bytes(round(c * 255) for c in clipped),
                           '/Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceCMYK')
            c, m, y, k = [Fraction(round(c * 255), 255) for c in clipped]
            expected_image = (quantize(1 - min(1, Fraction(3, 10) * c + Fraction(59, 100) * m + Fraction(11, 100) * y + k)),) * 3
            check('cmyk-image-' + operands,
                  fixture('18 0 0 12 0 0 cm /Im Do', group=gray_group,
                          attrs='/Resources << /XObject << /Im 6 0 R >> >>', extra={6: image}), expected_image)
            pattern = stream('0 0 3 3 re f', '/Type /Pattern /PatternType 1 /PaintType 2 '
                             '/TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>')
            check('cmyk-uncolored-pattern-' + operands,
                  fixture('/Pcs cs ' + operands + ' /P scn ' + rect, group=gray_group,
                          attrs='/Resources << /ColorSpace << /Pcs [/Pattern /DeviceCMYK] >> '
                                '/Pattern << /P 6 0 R >> >>', extra={6: pattern}), expected)
        for metric in ('18 0 d0', '18 0 0 0 18 12 d1'):
            font = (b'<< /Type /Font /Subtype /Type3 /FontBBox [0 0 18 12] '
                    b'/FontMatrix [1 0 0 1 0 0] /FirstChar 65 /LastChar 65 /Widths [18] '
                    b'/Encoding << /Differences [65 /A] >> /CharProcs << /A 7 0 R >> >>')
            check('cmyk-text-' + metric,
                  fixture('.8 0 0 .6 k BT /F 1 Tf (A) Tj ET', group=gray_group,
                          attrs='/Resources << /Font << /F 6 0 R >> >>',
                          extra={6: font, 7: stream(metric + ' ' + rect)}), (41, 41, 41))
        for sample in (1, 127, 9830, 32767, 32768, 65400):
            image = stream(sample.to_bytes(2, 'big'),
                           '/Subtype /Image /Width 1 /Height 1 /BitsPerComponent 16 /ColorSpace /DeviceGray')
            value = Fraction(sample, 65535)
            check('16-bit-image-' + str(sample),
                  fixture('/A gs 18 0 0 12 0 0 cm /Im Do /Im Do',
                          attrs='/Resources << /ExtGState << /A << /ca .5 >> >> '
                                '/XObject << /Im 6 0 R >> >>', extra={6: image}),
                  (quantize(Fraction(3, 4) * value + Fraction(1, 4)),) * 3)
        for kind in (4, 5, 6, 7):
            # A constant CMYK field isolates conversion from geometric interpolation.
            color = (.8, 0, 0, .6)
            if kind <= 5:
                data = triangle_records([(0, 0, 0, color), (0, 1, 0, color),
                                         (0, 0, 1, color), (1, 1, 1, color)], kind=kind)
            else:
                data = patch_records([(0, control_points(kind), [color] * 4)], kind=kind)
            fields = (f'/ShadingType {kind} /ColorSpace /DeviceCMYK /BitsPerCoordinate 8 '
                      '/BitsPerComponent 8 /Decode [0 18 0 12 0 1 0 1 0 1 0 1] ' +
                      ('/VerticesPerRow 2' if kind == 5 else '/BitsPerFlag 8'))
            shading = stream(data, fields)
            check('cmyk-mesh-' + str(kind),
                  fixture('/S sh', group=gray_group,
                          attrs='/Resources << /Shading << /S 6 0 R >> >>', extra={6: shading}),
                  (41, 41, 41))
        for value in (.001, .003, .499, .5, .501, .999):
            # Two half-opacity paints in a group must keep the input, not its byte image.
            group = '<< /S /Transparency /I true /CS /DeviceRGB >>'
            check('sub-byte-solid-' + str(value),
                  fixture(f'/A gs {value} g {rect} {rect}', group=group,
                          attrs='/Resources << /ExtGState << /A << /ca .5 >> >> >>'),
                  (quantize(.75 * value + .25),) * 3)
    print(f'PDF device colors: {checks} analytic checks passed')


if __name__ == '__main__':
    main()
