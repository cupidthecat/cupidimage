#!/usr/bin/env python3
"""Generate owned cross-format color fixtures (Pillow required only here)."""
import base64
import json
from pathlib import Path
import struct
import zlib
from PIL import Image
from test_pdf_pages import document

ROOT = Path(__file__).resolve().parents[1]
colors = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 255, 255)]
pixels = bytes(c for y in range(16) for x in range(16) for c in colors[(y // 8) * 2 + x // 8])
image = Image.frombytes('RGBA', (16, 16), pixels)
rows = []


def add(fmt, name, expected=pixels, tolerance=0, **options):
    path = ROOT / 'assets' / fmt / 'reference' / name
    path.parent.mkdir(parents=True, exist_ok=True)
    rows.append(dict(asset=str(path.relative_to(ROOT)), width=16, height=16,
                     rgba=base64.b64encode(expected).decode(), tolerance=tolerance, **options))
    return path


for fmt, ext, mode, options in [
    ('png', 'png', 'RGBA', {}), ('bmp', 'bmp', 'RGB', {}),
    ('gif', 'gif', 'RGB', {}), ('tga', 'tga', 'RGBA', {}),
    ('tiff', 'tif', 'RGBA', {'compression': 'raw'}),
    ('webp', 'webp', 'RGBA', {'lossless': True, 'exact': True}),
    ('ico', 'ico', 'RGBA', {'sizes': [(16, 16)]})]:
    image.convert(mode).save(add(fmt, 'quadrants.' + ext), **options)
# CUR uses the ICO container with type=2 and hotspot fields in place of planes/depth.
ico = bytearray((ROOT / 'assets/ico/reference/quadrants.ico').read_bytes())
struct.pack_into('<H', ico, 2, 2)
struct.pack_into('<HH', ico, 10, 3, 5)
add('ico', 'quadrants.cur', hotspot=[3, 5]).write_bytes(ico)
for progressive in (False, True):
    path = add('jpeg', 'quadrants-' + ('progressive' if progressive else 'baseline') + '.jpg', tolerance=3)
    image.convert('RGB').save(path, quality=95, subsampling=0, progressive=progressive)
    # Independent libjpeg pixels; allow small integer IDCT rounding differences.
    rows[-1]['rgba'] = base64.b64encode(Image.open(path).convert('RGBA').tobytes()).decode()
svg = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16">'
for i, color in enumerate(('red', 'lime', 'blue', 'white')):
    svg += f'<rect x="{(i%2)*8}" y="{(i//2)*8}" width="8" height="8" fill="{color}"/>'
add('svg', 'quadrants.svg').write_text(svg + '</svg>\n')
stream = '1 0 0 rg 0 6 6 6 re f 0 1 0 rg 6 6 6 6 re f 0 0 1 rg 0 0 6 6 re f 1 1 1 rg 6 0 6 6 re f'
pdf_data = document(page_attrs='/MediaBox [0 0 12 12]', stream=stream)
add('pdf', 'quadrants.pdf').write_bytes(pdf_data)
add('pdf', 'eof-without-newline.pdf').write_bytes(pdf_data.rstrip(b'\r\n'))
add('pdf', 'marked-content-properties.pdf').write_bytes(document(
    page_attrs='/MediaBox [0 0 12 12]', stream='/Span << /MCID 0 /Nested << /Values [1 (>> [) <4142>] >> >> BDC ' + stream + ' EMC'))
compressed = zlib.compress(stream.encode())
add('pdf', 'indirect-flate-length.pdf').write_bytes(document(
    page_attrs='/MediaBox [0 0 12 12]', extra={
        4: b'<< /Length 5 0 R /Filter /FlateDecode >>\nstream\n' + compressed + b'\nendstream',
        5: str(len(compressed))}))
(ROOT / 'assets/pdf/minimal_xref.pdf').write_bytes(document(
    page_attrs='/MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >>',
    stream='BT\n/F1 12 Tf\n100 700 Td\n(Hello) Tj\nET\n',
    extra={5: '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'}))
(ROOT / 'tests/fixtures/format-reference.json').write_text(json.dumps({
    'provenance': 'Owned synthetic quadrant pixels. Lossless and vector expectations are analytic; JPEG expectations use Pillow/libjpeg with tolerance 3 for integer IDCT rounding.',
    'cases': rows}, indent=2) + '\n')
print('Generated', len(rows), 'cross-format references')
