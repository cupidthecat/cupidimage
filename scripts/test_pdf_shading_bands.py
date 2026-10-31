#!/usr/bin/env python3
"""Compare bounded shading bands with complete-page caches and literal pixels."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_type3 import stream


def page(kind=2, full=False, space='DeviceRGB', origin=(0, 0), matrix='1 0 0 1 0 0',
         width=18, height=120, two=False):
    x, y = origin
    colors = {'DeviceRGB': ('0 0 1', '1 0 0'),
              'DeviceCMYK': ('1 1 0 0', '0 1 1 0'), 'DeviceGray': ('0', '1')}
    a, b = colors[space]
    function = f'<< /FunctionType 2 /Domain [0 1] /C0 [{a}] /C1 [{b}] /N 1 >>'
    if kind == 1:
        code = {'DeviceRGB': '{ pop dup 1 exch sub 0 exch }',
                'DeviceCMYK': '{ pop 1 exch sub 1 0 0 }', 'DeviceGray': '{ pop }'}[space]
        components = len(a.split())
        function = stream(code, '/FunctionType 4 /Domain [0 1 0 120] /Range [' +
                          '0 1 ' * components + ']')
        definition = '/ShadingType 1 /Domain [0 1 0 120] /Matrix [18 0 0 1 0 0]'
    elif kind == 2:
        definition = '/ShadingType 2 /Coords [0 0 18 120] /Extend [true true]'
    else:
        definition = '/ShadingType 3 /Coords [9 60 0 9 60 90] /Extend [true true]'
    state = '/ExtGState << >> ' if full else ''
    pattern = f'<< /PatternType 2 {state}/Matrix [{matrix}] /Shading 7 0 R >>'
    objects = {5: pattern, 6: pattern.replace('0 0]', '.75 0]'),
               7: f'<< {definition} /ColorSpace /{space} /Background [{a}] /Function 8 0 R >>',
               8: function}
    # Alternate cached bands, revisit the first band, then restore a saved pattern.
    content = (f'/Pattern cs /P scn {x} {y} {width} {height} re f q ' +
               ('/Q scn ' if two else '') +
               f'{x} {y + height - 12} {width} 12 re f Q {x} {y} {width} 12 re f')
    return document(f'/MediaBox [{x} {y} {x + width} {y + height}] '
                    f'/Group << /S /Transparency /CS /{space} >> '
                    '/Resources << /Pattern << /P 5 0 R /Q 6 0 R >> >>',
                    stream=content, extra=objects)


def main():
    probe, allocator = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupid-shading-bands-') as directory:
        path = Path(directory) / 'case.pdf'

        def render(data, aa=False):
            nonlocal checks
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), '0', *(['1', '1', '1', '1', '1'] if aa else [])],
                                    capture_output=True, timeout=60)
            assert result.returncode == 0, result.stderr
            checks += 1
            return result.stdout

        for kind in (1, 2, 3):
            for space in ('DeviceRGB', 'DeviceCMYK', 'DeviceGray'):
                for origin, matrix in (((0, 0), '1 0 0 1 0 0'),
                                       ((-.125, -.125), '1 .25 .125 1 -.125 -.125'),
                                       ((0, -128.125), '1 0 0 1 0 -128.125')):
                    for aa in (False, True):
                        kwargs = dict(kind=kind, space=space, origin=origin, matrix=matrix, two=True)
                        assert render(page(**kwargs), aa) == render(page(full=True, **kwargs), aa), kwargs
        # The unused rows after the last band must not evaluate a function.
        def last_band(full):
            function = stream('{ dup 0 lt { 0 0 div pop } if pop pop 1 0 0 }',
                              '/FunctionType 4 /Domain [0 1 -100 100] /Range [0 1 0 1 0 1]')
            state = '/ExtGState << >> ' if full else ''
            return document('/MediaBox [0 0 .75 54] /Resources << /Pattern << /P 5 0 R >> >>',
                            stream='/Pattern cs /P scn 0 0 .75 54 re f',
                            extra={5: '<< /PatternType 2 ' + state + '/Shading 6 0 R >>',
                                   6: '<< /ShadingType 1 /ColorSpace /DeviceRGB /Domain [0 1 -100 100] /Function 7 0 R >>',
                                   7: function})
        band = render(last_band(False))
        assert band == render(last_band(True))
        assert band[12:] == bytes((255, 0, 0, 255)) * 72
        # Two full CMYK page caches exceed 64 MiB; two exact row bands do not.
        large = page(space='DeviceCMYK', width=612, height=792, two=True).replace(
            b'/C0 [1 1 0 0]', b'/C0 [0 1 1 0]')
        output = render(large)
        assert struct.unpack('=III', output[:12]) == (816, 1056, 1)
        assert output[12:] == bytes((255, 0, 0, 255)) * (816 * 1056)
        # Fault injection uses a narrow page that still crosses a band boundary.
        for full in (False, True):
            data = page(width=.75, height=54, full=full, two=True)
            path.write_bytes(data)
            result = subprocess.run([allocator, str(path), '0'], capture_output=True, text=True,
                                    check=True, timeout=180)
            allocations += int(result.stdout.split('checks')[0].split()[-1])
    print(f'PDF shading bands: {checks} page checks; {allocations} allocation checks')


if __name__ == '__main__':
    main()
