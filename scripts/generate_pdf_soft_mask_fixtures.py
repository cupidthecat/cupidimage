#!/usr/bin/env python3
"""Generate original graphics-state masks with analytic RGBA expectations."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_soft_masks import fixture,A
root=Path(__file__).resolve().parents[1]
cases=[
    ('graphics-alpha-mask.pdf',fixture(mask_program='/A gs 0 0 8.8 12 re f'),(255,127,127)),
    ('graphics-luminosity-mask.pdf',fixture(mask_program='0 1 0 rg 0 0 8.8 12 re f',content='/M gs 0 0 1 rg 0 0 18 12 re f',subtype='Luminosity'),(105,105,255)),
    ('graphics-gray-mask-transfer.pdf',fixture(mask_program=A+' g 0 0 8.8 12 re f',subtype='Luminosity',space='/DeviceGray',mask_extra='/BC [1] /TR << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>'),(255,128,128)),
]
path=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(path.read_text())
for name,pdf,color in cases:
    asset='assets/pdf/reference/'+name
    (root/asset).write_bytes(pdf)
    pixels=bytes(v for y in range(16) for x in range(24) for v in (*(color if x<12 else (255,255,255)),255))
    rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':24,'height':16,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
path.write_text(json.dumps(rows,indent=2)+'\n')
