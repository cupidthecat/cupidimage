#!/usr/bin/env python3
"""Check clipped shading scan bounds without changing sampled colors."""
import itertools
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_type3 import stream


def page(kind=2, repeats=1, clip='3 3 9 6 re W n', size=(612, 792), prefix=''):
    if kind == 1:
        shade = '/ShadingType 1 /Domain [0 1000 0 1000]'
        function = stream('{ pop pop 0 1 0 }',
                          '/FunctionType 4 /Domain [0 1000 0 1000] /Range [0 1 0 1 0 1]')
    else:
        shade = ('/ShadingType 2 /Coords [0 0 18 0]' if kind == 2 else
                 '/ShadingType 3 /Coords [0 0 0 0 0 18]') + ' /Extend [true true]'
        function = '<< /FunctionType 2 /Domain [0 1] /C0 [0 1 0] /C1 [0 1 0] /N 1 >>'
    resources = '/Resources << /Shading << /S 5 0 R >> >>'
    return document(f'/MediaBox [0 0 {size[0]} {size[1]}] ' + resources,
                    stream=prefix + clip + ' ' + '/S sh ' * repeats,
                    extra={5: '<< ' + shade + ' /ColorSpace /DeviceRGB /Function 6 0 R >>',
                           6: function})


def main():
    probe, allocator = [str(Path(x).resolve()) for x in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupid-shading-work-') as directory:
        path = Path(directory) / 'case.pdf'
        def render(data, aa=False, reject=False):
            nonlocal checks
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), '0',
                                     *(['--pdf-text-aa', '--pdf-graphics-aa'] if aa else [])],
                                    capture_output=True, timeout=60)
            assert (result.returncode != 0) == reject, (result.returncode, result.stderr)
            checks += 1
            if reject:
                return None
            w, h, n = struct.unpack('<III', result.stdout[:12])
            assert n == 1 and len(result.stdout) == 12 + w * h * 4
            return w, h, result.stdout[12:]
        for kind, aa in itertools.product((1, 2, 3), (False, True)):
            got = render(page(kind, 192), aa)
            assert got == render(page(kind), aa)
            w, h, pixels = got
            expected = bytes(v for y in range(h) for x in range(w) for v in
                             ((0, 255, 0, 255) if 4 <= x < 16 and h-12 <= y < h-4 else (255,) * 4))
            assert pixels == expected, (kind, aa)
        # The small direct-budget test checks exhaustion without painting tens
        # of millions of pixels under sanitizers on shared CI runners.
        if '--slow-limit' in sys.argv[3:]:
            render(page(2, 192, clip=''), reject=True)
        for size, clip, aa in itertools.product(((5.25, 3.75), (6.75, 5.25)),
                ('0 0 .75 .75 re W n', '0 0 0 0 re W n',
                 '.75 .75 .75 1.5 re 3 0 .75 3 re W* n'), (False, True)):
            for kind in (1, 2, 3):
                assert render(page(kind, 20, clip, size), aa) == render(page(kind, 1, clip, size), aa)
        # The clipping bitmap can contain fractional coverage; repeated painting
        # is deliberately excluded here because it changes edge opacity.
        for aa in (False, True):
            for kind in (2, 3):
                data = page(kind, clip='.125 .25 2.5 1.5 re W n', size=(5.25, 3.75))
                render(data, aa)
                path.write_bytes(data)
                result = subprocess.run([allocator, str(path), '0', str(int(aa))],
                                        capture_output=True, text=True, check=True, timeout=120)
                allocations += int(result.stdout.split('checks')[0].split()[-1])
    print(f'PDF shading work: {checks} page checks; {allocations} allocation checks')


if __name__ == '__main__':
    main()
