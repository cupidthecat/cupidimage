#!/usr/bin/env python3
"""Generate original page-isolation and gray-blending regression assets."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_page_groups import fixture

root=Path(__file__).resolve().parents[1]
cases=[
    ('isolated-page-screen.pdf',fixture('/G gs 1 0 0 rg 0 0 18 12 re f',
        resources='/ExtGState << /G << /BM /Screen /ca .5 >> >>'),(255,128,128)),
    ('gray-page-group.pdf',fixture('1 0 0 rg 0 0 18 12 re f',
        '<< /S /Transparency /CS /DeviceGray /I false >>'),(77,77,77)),
]
path=root/'tests/fixtures/pdf-image-reference.json'
rows=json.loads(path.read_text())
for name,pdf,color in cases:
    asset='assets/pdf/reference/'+name
    (root/asset).write_bytes(pdf)
    pixels=bytes((*color,255))*(24*16)
    rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':24,'height':16,
                         'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
path.write_text(json.dumps(rows,indent=2)+'\n')
