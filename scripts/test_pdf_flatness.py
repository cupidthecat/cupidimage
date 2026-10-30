#!/usr/bin/env python3
"""Exercise device-space flatness through complete PDF documents."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_type3 import fixture as type3_fixture, stream
from test_pdf_embedded_fonts import fixture as font_fixture


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = allocations = 0
    curve = '5 5 m 5 75 75 75 75 5 c h '
    def fixture(content, resources='', extra=None):
        return document('/MediaBox [0 0 120 90] /Resources << '+resources+' >>',
                        stream=content, extra=extra)
    with tempfile.TemporaryDirectory(prefix='cupidimage-flatness-') as directory:
        path = Path(directory) / 'case.pdf'
        def render(data, reject=False):
            nonlocal checks
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=15)
            assert result.returncode == (3 if reject else 0), result.stderr
            checks += 1
            return result.stdout
        def same(a, b):
            assert render(a) == render(b), 'flatness state or scope differs'
        for paint in ('f', 'f*', 'S', 's', 'B', 'B*', 'b', 'b*', 'W n 0 0 120 90 re f'):
            for value in ('0', '.001', '.01', '100'):
                direct = fixture(value+' i '+curve+paint)
                for entry, extra in ((value, None), ('10 0 R', {10:value})):
                    same(direct, fixture('/Style gs '+curve+paint,
                         '/ExtGState << /Style << /FL '+entry+' >> >>', extra))
                same(direct, fixture(value+' i q .1 i Q '+curve+paint))
            same(fixture('.001 i '+curve+paint),
                 fixture('.00000000000000000001 i '+curve+'.001 i '+paint))
            render(fixture(curve+'.00000000000000000001 i '+paint), reject=True)
            same(fixture('.001 i '+curve+paint),
                 fixture(curve+'/Style gs '+paint,
                         '/ExtGState << /Style << /FL .001 >> >>'))
            same(fixture('.001 i '+curve+paint),
                 fixture('.001 i '+curve+'q .00000000000000000001 i Q '+paint))
            for bad in ('-1', '101', '/Name', 'true', '[]'):
                render(fixture('/Style gs '+curve+paint,
                       '/ExtGState << /Style << /FL '+bad+' >> >>'), reject=True)
            render(fixture('.00000000000000000001 i '+curve+paint), reject=True)
            same(fixture(curve+paint), fixture('/Style gs '+curve+paint,
                 '/ExtGState << /Style << /FL null >> >>'))
            # A Form inherits the caller's flatness and cannot leak its changes.
            form = stream(curve+paint, '/Type /XObject /Subtype /Form /BBox [0 0 120 90]')
            same(fixture('.001 i '+curve+paint), fixture('.001 i /X Do',
                 '/XObject << /X 10 0 R >>', {10:form}))
            empty = stream('.00000000000000000001 i',
                           '/Type /XObject /Subtype /Form /BBox [0 0 120 90]')
            same(fixture('.001 i '+curve+paint), fixture('.001 i /X Do '+curve+paint,
                 '/XObject << /X 10 0 R >>', {10:empty}))
        # Device coordinates are captured during construction, not at paint time.
        same(fixture('.001 i '+curve+'f'),
             fixture('.001 i '+curve+'2 0 0 3 4 5 cm f'))
        # A final empty subpath must not close a preceding curved subpath.
        same(fixture('.001 i '+curve+'20 20 m s'),
             fixture('.001 i '+curve+'S'))
        # Curved native glyph strokes use the same device-pixel tolerance.
        for mode in (1, 2, 5, 6):
            text = f'BT /F 60 Tf {mode} Tr 20 20 Td (C) Tj ET'
            same(font_fixture('.001 i '+text), font_fixture('.001 i q .1 i Q '+text))
            render(font_fixture('.00000000000000000001 i '+text), reject=True)
        program = '600 0 d0 0 0 m 0 700 500 700 500 0 c h f'
        text = 'BT /F 60 Tf 20 20 Td (A) Tj ET'
        same(type3_fixture('.001 i '+text, programs=[program, program]),
             type3_fixture(text, programs=[program.replace('d0 ', 'd0 .001 i '), program]))
        render(type3_fixture('.00000000000000000001 i '+text,
                             programs=[program, program]), reject=True)
        if len(sys.argv) > 2:
            allocator = str(Path(sys.argv[2]).resolve())
            for paint in ('f', 'S', 'B', 'W n 0 0 120 90 re f'):
                for reject in (False, True):
                    precision = '.00000000000000000001' if reject else '.001'
                    path.write_bytes(fixture(curve+precision+' i '+paint))
                    result = subprocess.run([allocator, str(path), str(int(reject))],
                                            capture_output=True, text=True, timeout=30)
                    assert result.returncode == 0, result.stdout+result.stderr
                    allocations += int(result.stdout.split(': ')[1].split(' checks')[0])
    print(f'PDF flatness documents: {checks} rendering and rejection checks passed; '
          f'{allocations} allocation checks')


if __name__ == '__main__':
    main()
