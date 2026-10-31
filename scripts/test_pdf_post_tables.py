#!/usr/bin/env python3
"""Exercise legacy and high-index post names through complete PDF rendering."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_embedded_fonts import FONT, fixture
from test_pdf_truetype import replace_tables
from test_pdf_type3 import stream


def named_font(kind):
    header = lambda version: struct.pack('>I', version) + bytes(28)
    if kind == 'aat4':
        post = header(0x40000) + struct.pack('>8H', 65535, 0x41, 0x8140, *([65535] * 5))
        names = ('a8140', 'a0041')
    else:
        post = (header(0x20000) + struct.pack('>9H', 8, 0, 36, 65535, *([0] * 5)) +
                b'\x01x' * (65535 - 258) + b'\x08farGlyph')
        names = ('farGlyph', 'A')
    return replace_tables(FONT, {b'post': post}), names


def page(content, font, names, extra=None):
    encoding = '/Encoding << /BaseEncoding /WinAnsiEncoding /Differences [65 /' + ' /'.join(names) + '] >>'
    dictionary = ('<< /Type /Font /Subtype /TrueType /BaseFont /CupidImageTestShapes '
                  '/FontDescriptor 8 0 R /FirstChar 65 /LastChar 66 /Widths [300 700] '
                  '/ToUnicode 12 0 R ' + encoding + ' >>')
    objects = {5: dictionary, 12: stream('1 beginbfchar <41> <005a> endbfchar')}
    objects.update(extra or {})
    return fixture(content, font_data=font, extra=objects)


def main():
    probe, allocator = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupid-post-tables-') as directory:
        path = Path(directory) / 'case.pdf'

        def render(data, aa=False):
            nonlocal checks
            path.write_bytes(data)
            run = subprocess.run([probe, str(path), '0', *(['1'] * 5 if aa else [])],
                                 capture_output=True, timeout=30)
            assert run.returncode == 0, run.stderr
            checks += 1
            return run.stdout

        for kind in ('aat4', 'high-index'):
            font, names = named_font(kind)
            for mode in range(8):
                content = f'.7 w BT /F 24 Tf {mode} Tr .5 Tc 95 Tz 1 Ts 1 .15 -.1 1 20 30 Tm (AB) Tj ET'
                if mode >= 4:
                    content += ' .2 .4 .8 rg 0 0 120 90 re f'
                for aa in (False, True):
                    assert render(page(content, font, names), aa) == render(page(content, FONT, ('B', 'A')), aa), (kind, mode, aa)
            path.write_bytes(page('BT /F 24 Tf 4 Tr 20 30 Td (AB) Tj ET 0 0 120 90 re f', font, names))
            result = subprocess.run([allocator, str(path), '0'], capture_output=True, text=True,
                                    check=True, timeout=120)
            allocations += int(result.stdout.split('checks')[0].split()[-1])
        font, _ = named_font('aat4')
        for names in (('aFFFF', 'a10000'), ('A0041', 'a0x41')):
            content = 'BT /F 24 Tf 20 30 Td (AB) Tj ET'
            assert render(page(content, font, names)) == render(page(content, FONT, ('.notdef', '.notdef')))
    print(f'PDF post tables: {checks} page checks; {allocations} allocation checks')


if __name__ == '__main__':
    main()
