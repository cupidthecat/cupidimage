#!/usr/bin/env python3
"""Generate original empty/long font resource names with analytic glyph pixels."""
import base64
import json
import math
from pathlib import Path
import zlib
from test_pdf_font_resources import escaped, named
from test_pdf_graphics_fonts import fixture

root=Path(__file__).resolve().parents[1]
reference=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(reference.read_text())
pixels=bytearray(b'\xff'*(160*120*4))
for y in range(56,80):
    # The shared embedded-font fixture translates user space by 1.125 points.
    for x in range(math.ceil((20+1.125)*4/3-.5), math.ceil((32+1.125)*4/3-.5)):
        pixels[(y*160+x)*4:(y*160+x)*4+3]=b'\0\0\0'
for filename,name in (('font-resource-empty.pdf',''),('font-resource-127.pdf',named(127))):
    path=root/'assets/pdf/reference'/filename
    path.write_bytes(fixture('BT /'+escaped(name)+' 30 Tf 20 30 Td (A) Tj ET',fonts='/'+name+' 5 0 R'))
    asset=str(path.relative_to(root));rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':160,'height':120,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
reference.write_text(json.dumps(rows,indent=2)+'\n')
print('Generated two font-name assets with analytic references')
