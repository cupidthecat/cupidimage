#!/usr/bin/env python3
"""Generate an original Type 3 base-encoding fixture and analytic pixel oracle."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_type3 import fixture
root=Path(__file__).resolve().parents[1]
asset='assets/pdf/reference/type3-base-encoding.pdf'
(root/asset).write_bytes(fixture('BT /F 20 Tf 1 0 0 1 10 20 Tm (A) Tj ET',subtype='8 0 R',encoding='<< /BaseEncoding 9 0 R >>',extra={8:b'/Type3',9:b'/MacRomanEncoding'}))
pixels=bytearray()
for y in range(120):
    for x in range(160):
        inside=10<=(x+.5)*.75<20 and 20<=90-(y+.5)*.75<34
        pixels+=bytes((0,0,0,255) if inside else (255,255,255,255))
path=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(path.read_text())
rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
rows['cases'].append({'asset':asset,'width':160,'height':120,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
path.write_text(json.dumps(rows,indent=2)+'\n')
