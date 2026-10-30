#!/usr/bin/env python3
"""Generate analytic text-overlap and uncolored glyph-shape fixtures."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_text_knockout import fixture

root=Path(__file__).resolve().parents[1]
path=root/'tests/fixtures/pdf-image-reference.json'
rows=json.loads(path.read_text())
cases=[
    ('text-object-knockout.pdf',fixture('/G gs 1 0 0 rg BT /F 1 Tf -18 Tc (AA) Tj ET')),
    ('type3-shape-opacity.pdf',fixture('/G gs 1 0 0 rg BT /F 1 Tf (A) Tj ET',
        program='18 0 0 0 18 12 d1 0 0 18 12 re f 0 0 18 12 re f')),
]
for name,pdf in cases:
    asset='assets/pdf/reference/'+name
    (root/asset).write_bytes(pdf)
    pixels=bytes((255,128,128,255))*(24*16)
    rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':24,'height':16,
                         'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
path.write_text(json.dumps(rows,indent=2)+'\n')
