#!/usr/bin/env python3
"""Generate owned single-colorant PDFs and analytic color references."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_separation import fixture, SPACE

root=Path(__file__).resolve().parents[1]
reference=root/'tests/fixtures/pdf-image-reference.json'
rows=json.loads(reference.read_text())
for name,content,space,color in (
    ('separation-tint.pdf','/S cs .5 scn 0 0 18 12 re f',SPACE,(255,128,128)),
    ('separation-all.pdf','/S cs .5 scn 0 0 18 12 re f',SPACE.replace('/Spot','/All'),(128,128,128)),
    ('separation-none.pdf','0 0 1 rg 0 0 18 12 re f /S cs 1 scn 0 0 18 12 re f',SPACE.replace('/Spot','/None'),(0,0,255)),
):
    path=root/'assets/pdf/reference'/name
    path.write_bytes(fixture(content,space=space))
    pixels=bytes((*color,255))*(24*16)
    asset=str(path.relative_to(root))
    rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':24,'height':16,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
reference.write_text(json.dumps(rows,indent=2)+'\n')
print('Generated three Separation assets and analytic color references')
