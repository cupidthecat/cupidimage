#!/usr/bin/env python3
"""Check shading-pattern graphics state and non-isolated knockout composition."""
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
from test_pdf_mesh import records as mesh_records
from test_pdf_patch import records as patch_records, control_points


RED = ('<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] '
       '/Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [1 0 0] /N 1 >> >>')


def byte(value):
    return int(value * 255 + .5)


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-shading-patterns-') as tmp:
        path = Path(tmp) / 'case.pdf'

        def check(name, state='', caller=1, repetitions=1, expected=None, shading=RED,
                  content=None, extra=None, caller_state='', reject=False, gray=False, packet=None):
            nonlocal checks
            pattern = '<< /PatternType 2 /ExtGState << ' + state + ' >> /Shading 7 0 R >>'
            objects = {5: pattern, 7: shading}
            objects.update(extra or {})
            if content is None:
                content = ('0 0 1 rg 0 0 18 12 re f /A gs /Pattern cs /P scn ' +
                           '0 0 18 12 re f ' * repetitions)
            data = document('/MediaBox [0 0 18 12] ' + ('/Group << /S /Transparency /CS /DeviceGray >> ' if gray else '') + '/Resources << /Pattern << /P 5 0 R >> '
                            '/XObject << /F 6 0 R >> /ExtGState << /A << /ca ' + str(caller) +
                            ' /CA ' + str(caller) + ' ' + caller_state + ' >> >> >>',
                            stream=content, extra=objects)
            path.write_bytes(data if packet is None else packet)
            run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert run.returncode == (3 if reject else 0), (name, run.stderr)
            checks += 1
            if reject:
                return
            assert struct.unpack('=III', run.stdout[:12]) == (24, 16, 1)
            want = bytes(v for y in range(16) for x in range(24) for v in (*expected(x, y), 255))
            assert run.stdout[12:] == want, (name, next(
                (i, a, b) for i, (a, b) in enumerate(zip(run.stdout[12:], want)) if a != b))

        for mode in ('Normal', 'Multiply'):
            for alpha in (0, .25, .5, 1):
                for caller in (.5, 1):
                    for repetitions in (1, 2):
                        remain = (1 - alpha * caller) ** repetitions
                        pixel = (byte(1-remain) if mode == 'Normal' else 0, 0, byte(remain))
                        check(f'{mode}-{alpha}-{caller}-{repetitions}',
                              state=f'/ca {alpha} /BM /{mode}', caller=caller,
                              repetitions=repetitions, expected=lambda x, y: pixel)
        check('stroke-opacity', state='/ca .5 /CA .25', caller=.5,
              content='0 0 1 rg 0 0 18 12 re f /A gs /Pattern CS /P SCN 12 w 0 6 m 18 6 l S',
              expected=lambda x, y: (64, 0, 191))
        check('path-and-text-state-ignored', state='/CA .25 /LW 2 /D [[1 2] 0] /TK false /Font [8 0 R 12]',
              extra={8: b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'},
              expected=lambda x, y: (255, 0, 0))
        background = RED.replace('/Coords [0 0 18 0]', '/Coords [6 0 12 0] /Background [0 1 0]')
        for shape in (False, True):
            for alpha in (0, .5, 1):
                group = form('0 0 1 rg 0 0 18 12 re f /Pattern cs /P scn 0 0 18 12 re f',
                             '/BBox [0 0 18 12] /Group << /S /Transparency /I true /K true >> '
                             '/Resources << /Pattern << /P 5 0 R >> >>')
                pixel = ((byte(alpha), 0, byte(1-alpha)) if shape else
                         (255, byte(1-alpha), byte(1-alpha)))
                check(f'shape-{shape}-{alpha}', state=f'/ca {alpha} /AIS {str(shape).lower()}',
                      content='/F Do', extra={6: group}, expected=lambda x, y: pixel)
        mask = form('.5 g 0 0 18 12 re f',
                    '/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS /DeviceGray >>')
        check('inner-and-outer-masks', state='/ca .5 /SMask << /S /Luminosity /G 8 0 R >>', caller=.5,
              caller_state='/SMask << /S /Luminosity /G 8 0 R >>', extra={8: mask},
              expected=lambda x, y: (16, 0, 239))
        neutral = RED.replace('[1 0 0]', '[.5 .5 .5]')
        for mode, pixel in (
                ('Screen', (128, 128, 255)), ('Overlay', (0, 0, 255)),
                ('Darken', (0, 0, 128)), ('Lighten', (128, 128, 255)),
                ('ColorDodge', (0, 0, 255)), ('ColorBurn', (0, 0, 255)),
                ('HardLight', (0, 0, 255)), ('SoftLight', (0, 0, 255)),
                ('Difference', (128, 128, 128)), ('Exclusion', (128, 128, 128)),
                ('Hue', (28, 28, 28)), ('Saturation', (28, 28, 28)),
                ('Color', (28, 28, 28)), ('Luminosity', (112, 112, 255))):
            check('blend-' + mode, state='/BM /' + mode, shading=neutral,
                  expected=lambda x, y: pixel)
        gray_shading = RED.replace('/DeviceRGB', '/DeviceCMYK').replace('[1 0 0]', '[.8 0 0 .6]')
        for mode, value in (('Normal', 34), ('Multiply', 16)):
            check('gray-space-' + mode, state='/ca .5 /BM /' + mode,
                  shading=gray_shading, gray=True, expected=lambda x, y: (value,) * 3)
        half = lambda x, y: 8 <= x < 16
        plane = ('<< /ShadingType 1 /ColorSpace /DeviceRGB /Domain [6 12 0 12] '
                 '/Background [0 1 0] /Function 8 0 R >>')
        calculator = stream('{ pop pop 1 0 0 }',
                            '/FunctionType 4 /Domain [6 12 0 12] /Range [0 1 0 1 0 1]')
        radial = RED.replace('/ShadingType 2', '/ShadingType 3').replace(
            '/Coords [0 0 18 0]', '/Coords [9 6 0 9 6 6] /Background [0 1 0]')
        shadings = [(1, plane, half, {8: calculator}), (2, background, half, {}),
                    (3, radial, lambda x, y: (x+.5-12)**2+(y+.5-8)**2 <= 64, {})]
        for kind in (4, 5, 6, 7):
            if kind <= 5:
                vertices = [(0, 0, 0, (1, 0, 0)), (0, 1, 0, (1, 0, 0)),
                            (0, 0, 1, (1, 0, 0))]
                if kind == 5:
                    vertices.append((0, 1, 1, (1, 0, 0)))
                data = mesh_records(vertices, kind=kind)
                inside = (lambda x, y: (x+.5)/24 < (y+.5)/16) if kind == 4 else (lambda x, y: True)
            else:
                points = [(x * .5, y) for x, y in control_points(kind)]
                data = patch_records([(0, points, [(1, 0, 0)] * 4)], kind=kind)
                inside = lambda x, y: x < 12
            fields = (f'/ShadingType {kind} /ColorSpace /DeviceRGB /Background [0 1 0] '
                      '/BitsPerCoordinate 8 /BitsPerComponent 8 /Decode [0 18 0 12 0 1 0 1 0 1] ' +
                      ('/VerticesPerRow 2' if kind == 5 else '/BitsPerFlag 8'))
            shadings.append((kind, stream(data, fields), inside, {}))
        for kind, shading, inside, objects in shadings:
            check(f'geometry-alpha-{kind}', state='/ca .5', shading=shading, extra=objects,
                  expected=lambda x, y: (128, 0, 128) if inside(x, y) else (0, 128, 128))
            check(f'geometry-blend-{kind}', state='/ca .5 /BM /Multiply', shading=shading,
                  extra=objects, expected=lambda x, y: (0, 0, 128))
            check(f'geometry-shape-{kind}', state='/ca .5 /AIS true', shading=shading, extra=objects,
                  expected=lambda x, y: (128, 64, 64) if inside(x, y) else (0, 128, 128))
            check(f'geometry-blend-shape-{kind}', state='/ca .5 /BM /Multiply /AIS true',
                  shading=shading, extra=objects,
                  expected=lambda x, y: (0, 0, 64) if inside(x, y) else (0, 0, 128))
        check('background-mask-shape', state='/AIS true /SMask << /S /Luminosity /G 8 0 R >>',
              shading=background, extra={8: mask},
              expected=lambda x, y: (128, 64, 64) if 8 <= x < 16 else (0, 128, 128))
        for state, objects in (
                ('/ca .5 /BM /Mul#74iply', {}),
                ('/ca 8 0 R /BM 9 0 R', {8: b'.5', 9: b'/Multiply'}),
                ('/ca .5 /BM 8 0 R', {8: b'[/Future 9 0 R /Screen]', 9: b'/Mul#74iply'})):
            check('resolved-state-' + state, state=state, extra=objects,
                  expected=lambda x, y: (0, 0, 128))
        check('null-parameters', state='/ca 8 0 R /BM 8 0 R /AIS null /SMask null',
              extra={8: b'null'}, expected=lambda x, y: (255, 0, 0))
        for label, bodies, state, pixel in (
                ('name', [(8, b'/Mul#74iply')], '/ca .5 /BM 8 0 R', (0, 0, 128)),
                ('array', [(8, b'[/Future 6 0 R]'), (6, b'/Multiply')], '/ca .5 /BM 8 0 R', (0, 0, 128)),
                ('null', [(8, b'null')], '/ca 8 0 R /BM 8 0 R', (255, 0, 0))):
            header = b''
            body = b''
            for number, value in bodies:
                header += f'{number} {len(body)} '.encode()
                body += value + b' '
            objects = {
                1: b'<< /Type /Catalog /Pages 2 0 R >>',
                2: b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
                3: b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Resources << /Pattern << /P 7 0 R >> >> >>',
                4: stream('0 0 1 rg 0 0 18 12 re f /Pattern cs /P scn 0 0 18 12 re f'),
                5: stream(zlib.compress(header + body),
                          f'/Type /ObjStm /N {len(bodies)} /First {len(header)} /Filter /FlateDecode'),
                7: ('<< /PatternType 2 /ExtGState << ' + state + ' >> /Shading ' + RED + ' >>').encode()}
            check('compressed-state-' + label,
                  packet=object_pdf(objects, compressed=[number for number, _ in bodies]),
                  expected=lambda x, y: pixel)
        for state in ('/ca -1', '/ca 2', '/BM /Missing'):
            check('invalid-' + state, state=state, reject=True)
    print(f'PDF shading-pattern transparency: {checks} analytic checks passed')


if __name__ == '__main__':
    main()
