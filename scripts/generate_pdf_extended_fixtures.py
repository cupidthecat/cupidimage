#!/usr/bin/env python3
"""Generate owned extended-glyph pages and an analytic advance reference."""
import base64
import json
from pathlib import Path
import zlib
from generate_pdf_extended_fonts import FONTS
from generate_pdf_standard_metrics import metrics, SOURCE
from test_pdf_standard_fonts import fixture
from test_pdf_pages import document

root = Path(__file__).resolve().parents[1]
fonts = {5+i: f'<< /Type /Font /Subtype /Type1 /BaseFont /{name} /Encoding << /Differences [65 /abreve /uhungarumlaut /eogonek /Rcaron /Gbreve /notequal] >> >>'
         for i, name in enumerate(sorted(FONTS))}
resources = ' '.join(f'/F{i} {5+i} 0 R' for i in range(12))
content = []
for i, name in enumerate(sorted(FONTS)):
    x, y = 10 + (i // 6) * 150, 180 - (i % 6) * 30
    content.append(f'BT /F{i} 18 Tf 1 0 0 1 {x} {y} Tm (ABCDEF) Tj ET')
(root / 'assets/pdf/standard-extended-glyphs.pdf').write_bytes(document(
    '/MediaBox [0 0 300 210] /Resources << /Font << ' + resources + ' >> >>',
    stream='\n'.join(content), extra=fonts))
width = metrics(SOURCE / 'Helvetica.afm')[0]['abreve']
asset = root / 'assets/pdf/reference/standard-extended-advance.pdf'
asset.write_bytes(fixture(f'BT /F 30 Tf 3 Tr {60-2*width*.03:.8f} 30 Td (AA) Tj /Marker 30 Tf 0 Tr (A) Tj ET',
                          'Helvetica', '/Encoding << /Differences [65 /abreve] >>'))
pixels = bytearray(b'\xff' * (160*120*4))
for y in range(56, 80):
    for x in range(80, 96):
        pixels[(y*160+x)*4:(y*160+x)*4+3] = b'\0\0\0'
reference = root / 'tests/fixtures/pdf-image-reference.json'
rows = json.loads(reference.read_text())
name = str(asset.relative_to(root))
rows['cases'] = [r for r in rows['cases'] if r['asset'] != name]
rows['cases'].append({'asset': name, 'width': 160, 'height': 120,
                      'rgba_zlib': base64.b64encode(zlib.compress(pixels)).decode(), 'tolerance': 0})
reference.write_text(json.dumps(rows, indent=2) + '\n')
print('Generated two extended-font assets; advance oracle is analytic, visible glyph page is unverified')
