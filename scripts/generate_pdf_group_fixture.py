#!/usr/bin/env python3
"""Generate an original isolated-opacity group and its analytic pixel oracle."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_groups import fixture
root=Path(__file__).resolve().parents[1]
asset='assets/pdf/reference/isolated-opacity-group.pdf'
(root/asset).write_bytes(fixture('1 0 0 rg 0 0 11.8 12 re f 0 0 1 rg 6 0 12 12 re f','/A gs /G Do'))
pixels=bytes(v for y in range(16) for x in range(24) for v in ((255,127,127,255) if x<8 else (127,127,255,255)))
path=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(path.read_text())
rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
rows['cases'].append({'asset':asset,'width':24,'height':16,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
path.write_text(json.dumps(rows,indent=2)+'\n')
