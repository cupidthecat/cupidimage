#!/usr/bin/env python3
"""Generate an original object-stream font-state fixture and rectangle oracle."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_graphics_fonts import compressed_fixture
root=Path(__file__).resolve().parents[1]
asset='assets/pdf/reference/graphics-state-font-object-stream.pdf'
(root/asset).write_bytes(compressed_fixture('BT /Use gs 20 30 Td (A) Tj ET'))
pixels=bytearray(b'\xff'*(160*120*4))
for y in range(56,80):
    for x in range(28,44):pixels[(y*160+x)*4:(y*160+x)*4+3]=b'\0\0\0'
path=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(path.read_text())
rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
rows['cases'].append({'asset':asset,'width':160,'height':120,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
path.write_text(json.dumps(rows,indent=2)+'\n')
