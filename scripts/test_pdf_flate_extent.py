#!/usr/bin/env python3
"""Check the single trailing EOL permitted after PDF compressed stream data."""
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_type3 import stream
from test_pdf_embedded_fonts import FONT, fixture as truetype
from test_pdf_cff_render import BOX as CFF_BOX, CURVE as CFF_CURVE, fixture as cff_pdf
from test_pdf_cff import fixture as cff_font
from test_pdf_type1_render import fixture as type1_pdf, BOX, CURVE, WIDTH
from test_pdf_type1 import fixture as type1_font


def main():
    probe, allocator, inflater = [str(Path(arg).resolve()) for arg in sys.argv[1:4]]
    checks = allocations = 0
    text = 'BT /F 30 Tf 20 30 Td (A) Tj ET'
    cff = cff_font(programs=[b'\x0e', b'\x0e', CFF_BOX, CFF_CURVE])
    type1, l1, l2 = type1_font(glyphs=[('.notdef', WIDTH+b'\x0e'),
                                      ('space', WIDTH+b'\x0e'), ('A', BOX), ('B', CURVE)])
    raw = b'1 0 0 rg 0 0 18 12 re f'
    def content(payload):
        return document('/MediaBox [0 0 18 12]',
                        extra={4:stream(payload, '/Filter /FlateDecode')})
    def chained(payload):
        return document('/MediaBox [0 0 18 12]',
                        extra={4:stream(payload.hex().encode()+b'>',
                                        '/Filter [/ASCIIHexDecode /FlateDecode]')})
    def picture(payload):
        return document('/MediaBox [0 0 18 12] /Resources << /XObject << /Im 5 0 R >> >>',
                        stream='18 0 0 12 0 0 cm /Im Do',
                        extra={5:stream(payload, '/Type /XObject /Subtype /Image /Width 1 '
                                        '/Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB '
                                        '/Filter /FlateDecode')})
    builders = [
        (raw, content), (raw, chained), (b'\xff\0\0', picture),
        (FONT, lambda payload: truetype(text, extra={9:stream(payload, '/Filter /FlateDecode')})),
        (cff, lambda payload: cff_pdf(text, extra={9:stream(payload, '/Filter /FlateDecode /Subtype /Type1C')})),
        (type1, lambda payload: type1_pdf(text, extra={9:stream(payload,
                  f'/Filter /FlateDecode /Length1 {l1} /Length2 {l2} /Length3 0')})),
    ]
    with tempfile.TemporaryDirectory(prefix='cupidimage-flate-extent-') as directory:
        path = Path(directory)/'case.pdf'
        encoded_path = Path(directory)/'stream.zlib'
        def render(data, reject=False):
            nonlocal checks
            path.write_bytes(data)
            run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=15)
            assert run.returncode == (3 if reject else 0), run.stderr
            checks += 1
            return run.stdout
        for source, builder in builders:
            encoded = zlib.compress(source)
            baseline = render(builder(encoded))
            for suffix in (b'\n', b'\r', b'\r\n'):
                assert render(builder(encoded+suffix)) == baseline
                # Shared non-PDF decoders still require exactly one complete zlib stream.
                encoded_path.write_bytes(encoded+suffix)
                run = subprocess.run([inflater, str(encoded_path), str(len(source))],
                                     capture_output=True, timeout=15)
                assert run.returncode == 1 and not run.stdout
                checks += 1
            for suffix in (b' ', b'\0', b'\n\n', b'\r\r', b'\n\r', b'x', zlib.compress(b'extra')):
                render(builder(encoded+suffix), True)
            corrupt = encoded[:-1]+bytes([encoded[-1]^1])
            for payload in (corrupt+b'\n', encoded[:-2]+b'\r\n'):
                render(builder(payload), True)
            for payload, reject in ((encoded+b'\r\n', False), (encoded+b'x', True)):
                path.write_bytes(builder(payload))
                run = subprocess.run([allocator, str(path), str(int(reject))],
                                     capture_output=True, text=True, timeout=30)
                assert run.returncode == 0, run.stdout+run.stderr
                allocations += int(run.stdout.split(': ')[1].split(' checks')[0])
    print(f'PDF Flate extent: {checks} checks passed; {allocations} allocation checks')


if __name__ == '__main__':
    main()
