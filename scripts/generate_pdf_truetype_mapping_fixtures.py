#!/usr/bin/env python3
"""Generate authored TrueType mapping fixtures with analytic rectangle pixels."""
import base64
import json
from pathlib import Path
import struct
import zlib
from test_pdf_embedded_fonts import FONT,fixture
from test_pdf_truetype import replace_tables,tables
from test_pdf_truetype_cmaps import format10
root=Path(__file__).resolve().parents[1]
cmap=struct.pack('>HHHHI',0,1,3,10,12)+format10(0x10000,(1,))
names=json.loads((root/'tests/fixtures/pdf-post-names.json').read_text())['names']
_,at,_=tables(FONT)[b'maxp'];count=struct.unpack_from('>H',FONT,at+4)[0]
post=struct.pack('>I',0x20000)+bytes(28)+struct.pack('>H',count)+struct.pack('>'+str(count)+'H',0,names.index('Delta'),*([0]*(count-2)))
cases=[('embedded-truetype-cmap10.pdf',{b'cmap':cmap},'<< /Differences [65 /u10000] >>'),
       ('embedded-truetype-post-names.pdf',{b'cmap':b'\0\0\0\0',b'post':post},'<< /Differences [65 /Delta] >>')]
pixels=bytearray(b'\xff'*(160*120*4))
# The fixture's 1.125-point translation puts the authored box at x=28..43 pixels.
for y in range(56,80):
    for x in range(28,44):pixels[(y*160+x)*4:(y*160+x)*4+3]=b'\0\0\0'
reference=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(reference.read_text())
for name,changes,encoding in cases:
    path=root/'assets/pdf/reference'/name
    path.write_bytes(fixture('BT /F 30 Tf 20 30 Td (A) Tj ET',font_data=replace_tables(FONT,changes),encoding=encoding))
    asset=str(path.relative_to(root));rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':160,'height':120,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
reference.write_text(json.dumps(rows,indent=2)+'\n')
print('Generated two TrueType mapping assets and analytic rectangle references')
