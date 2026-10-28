#!/usr/bin/env python3
"""Generate owned glyph-origin fixtures and analytic raster references."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_embedded_fonts import fixture
from test_pdf_truetype_bearings import bearing_font

root=Path(__file__).resolve().parents[1]
reference=root/'tests/fixtures/pdf-image-reference.json'
rows=json.loads(reference.read_text())
for name,font,left in (
    ('embedded-truetype-bearing.pdf',bearing_font({1:100},box_x=80),32),
    ('embedded-truetype-short-bearing.pdf',bearing_font({1:-200},short=True),20),
):
    path=root/'assets/pdf/reference'/name
    path.write_bytes(fixture('BT /F 30 Tf 20 30 Td (AA) Tj ET',font_data=font))
    pixels=bytearray(b'\xff'*(160*120*4))
    for y in range(56,80):
        for start in (left,left+40):
            for x in range(start,start+16):pixels[(y*160+x)*4:(y*160+x)*4+3]=b'\0\0\0'
    asset=str(path.relative_to(root))
    rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':160,'height':120,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
reference.write_text(json.dumps(rows,indent=2)+'\n')
print('Generated two TrueType bearing assets and analytic references')
