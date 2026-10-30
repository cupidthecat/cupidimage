#!/usr/bin/env python3
"""Create a spacing-mark sample and an analytic standard-font advance asset."""
import base64
import json
from pathlib import Path
import zlib

from generate_pdf_standard_marks import SOURCE
from generate_pdf_standard_metrics import metrics, SOURCE as AFMS
from test_pdf_pages import document
from test_pdf_standard_fonts import fixture

root = Path(__file__).resolve().parents[1]
rows = json.loads((SOURCE / 'manifest.json').read_text())['fonts']
fonts = {5 + i: f'<< /Type /Font /Subtype /Type1 /BaseFont /{row["standard_name"]} '
                  '/Encoding << /Differences [65 /A /abreve /commaaccent /B] >> >>'
         for i, row in enumerate(rows)}
fonts[13] = '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'
resources = ' '.join(f'/F{i} {5+i} 0 R' for i in range(8)) + ' /Label 13 0 R'
content = []
for i, row in enumerate(rows):
    x, y = 15 + (i // 4) * 150, 180 - (i % 4) * 45
    content.append(f'BT /Label 12 Tf 1 0 0 1 {x} {y+26} Tm ({row["standard_name"]}) Tj ET')
    content.append(f'BT /F{i} 24 Tf 1 0 0 1 {x} {y} Tm (ABCD) Tj ET')
(root / 'assets/pdf/standard-spacing-marks.pdf').write_bytes(document(
    '/MediaBox [0 0 300 225] /Resources << /Font << ' + resources + ' >> >>',
    stream='\n'.join(content), extra=fonts))
asset = root / 'assets/pdf/reference/standard-spacing-advance.pdf'
width = metrics(AFMS / 'Times-Roman.afm')[0]['commaaccent']
asset.write_bytes(fixture(f'BT /F 30 Tf 3 Tr {60-2*width*.03:.8f} 30 Td (AA) Tj '
                         '/Marker 30 Tf 0 Tr (A) Tj ET', 'Times-Roman',
                         '/Encoding << /Differences [65 /commaaccent] >>'))
pixels = bytearray(b'\xff' * (160 * 120 * 4))
for y in range(56, 80):
    for x in range(80, 96):
        pixels[(y*160+x)*4:(y*160+x)*4+3] = b'\0\0\0'
reference = root / 'tests/fixtures/pdf-image-reference.json'
expected = json.loads(reference.read_text())
name = str(asset.relative_to(root))
expected['cases'] = [row for row in expected['cases'] if row['asset'] != name]
expected['cases'].append({'asset': name, 'width': 160, 'height': 120,
                          'rgba_zlib': base64.b64encode(zlib.compress(pixels)).decode(), 'tolerance': 0})
reference.write_text(json.dumps(expected, indent=2) + '\n')
print('Generated spacing-mark sample (unverified raster) and analytic advance asset')
