#!/usr/bin/env python3
"""Generate original nonembedded standard-font pages and an analytic advance oracle."""
import base64
import json
from pathlib import Path
import zlib
from generate_pdf_standard_metrics import ROOT
from generate_pdf_standard_outlines import FONTS
from test_pdf_standard_fonts import fixture
from test_pdf_pages import document

asset=ROOT/'assets/pdf/reference/standard-font-metrics.pdf'
asset.write_bytes(fixture('BT /F 30 Tf 3 Tr 19.98 30 Td (AB) Tj /Marker 30 Tf 0 Tr (A) Tj ET','Helvetica'))
pixels=bytearray(b'\xff'*(160*120*4))
for y in range(56,80):
    for x in range(80,96):pixels[(y*160+x)*4:(y*160+x)*4+3]=b'\0\0\0'
reference=ROOT/'tests/fixtures/pdf-image-reference.json'
rows=json.loads(reference.read_text())
name=str(asset.relative_to(ROOT))
rows['cases']=[r for r in rows['cases'] if r['asset']!=name]
rows['cases'].append({'asset':name,'width':160,'height':120,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
reference.write_text(json.dumps(rows,indent=2)+'\n')
fonts={5+i:f'<< /Type /Font /Subtype /Type1 /BaseFont /{name} >>' for i,name in enumerate(sorted(FONTS))}
resources=' '.join(f'/F{i} {5+i} 0 R' for i in range(14))
content=[]
for i,name in enumerate(sorted(FONTS)):
    x=10+(i//7)*150;y=180-(i%7)*26
    content.append(f'BT /F{i} 10 Tf 1 0 0 1 {x} {y} Tm (AWi 0123) Tj ET')
(ROOT/'assets/pdf/standard-font-substitutes.pdf').write_bytes(document('/MediaBox [0 0 300 210] /Resources << /Font << '+resources+' >> >>',stream='\n'.join(content),extra=fonts))
print('Generated two standard-font PDF assets and the analytic marker reference')
