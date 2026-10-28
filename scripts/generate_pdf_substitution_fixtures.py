#!/usr/bin/env python3
"""Generate owned nonembedded-font samples and an analytic width override check."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_substitution import ALIASES
from test_pdf_standard_fonts import fixture
from test_pdf_pages import document

root = Path(__file__).resolve().parents[1]
objects = {}
resources = []
content = []
for i, (face, aliases) in enumerate(ALIASES.items()):
    number = 5 + 2*i
    name = aliases[0]
    objects[number] = f'<< /Type /Font /Subtype /TrueType /BaseFont /{name} /FontDescriptor {number+1} 0 R /Encoding /WinAnsiEncoding /FirstChar 0 /LastChar 255 /Widths [' + '600 '*256 + '] >>'
    objects[number+1] = f'<< /Type /FontDescriptor /FontName /{name} /Flags 32 /FontBBox [-2000 -2000 3000 3000] /ItalicAngle 0 /Ascent 2000 /Descent -1000 /CapHeight 1000 /StemV 80 >>'
    resources.append(f'/F{i} {number} 0 R')
    x, y = 10+(i//6)*150, 180-(i%6)*30
    content.append(f'BT /F{i} 18 Tf 1 0 0 1 {x} {y} Tm <41422080e9> Tj ET')
(root/'assets/pdf/nonembedded-latin-families.pdf').write_bytes(document(
    '/MediaBox [0 0 300 210] /Resources << /Font << '+' '.join(resources)+' >> >>',
    stream='\n'.join(content), extra=objects))
asset = root/'assets/pdf/reference/substitute-font-widths.pdf'
asset.write_bytes(fixture('BT /F 30 Tf 3 Tr 18 30 Td (AA) Tj /Marker 30 Tf 0 Tr (A) Tj ET',
    'Arial', '/Encoding /WinAnsiEncoding', '/FontDescriptor 10 0 R /FirstChar 65 /LastChar 65 /Widths [700]',
    extra={10: b'<< /Type /FontDescriptor /FontName /Arial /Flags 32 /FontBBox [-2000 -2000 3000 3000] /ItalicAngle 0 /Ascent 2000 /Descent -1000 /CapHeight 1000 /StemV 80 >>'}))
pixels=bytearray(b'\xff'*(160*120*4))
for y in range(56,80):
    for x in range(80,96):pixels[(y*160+x)*4:(y*160+x)*4+3]=b'\0\0\0'
reference=root/'tests/fixtures/pdf-image-reference.json'
rows=json.loads(reference.read_text());name=str(asset.relative_to(root))
rows['cases']=[r for r in rows['cases'] if r['asset']!=name]
rows['cases'].append({'asset':name,'width':160,'height':120,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
reference.write_text(json.dumps(rows,indent=2)+'\n')
print('Generated two substitution assets; width oracle is analytic, visible faces are unverified')
