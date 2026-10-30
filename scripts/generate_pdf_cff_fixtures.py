#!/usr/bin/env python3
"""Generate an original geometric CFF font for allocation-failure tests."""
from pathlib import Path
from test_pdf_cff import fixture
from test_pdf_cff_render import BOX, CURVE
raw=fixture(charset=b'\0\0\1\0\x22\0\x23',strings=(),programs=[b'\x0e',b'\x0e',BOX,CURVE])
root=Path(__file__).resolve().parents[1]
(root/'tests/fixtures/pdf-cff.inc').write_text('/* Original geometric CFF font; regenerate with generate_pdf_cff_fixtures.py. */\nstatic const unsigned char test_cff_font[] = {\n'+''.join('    '+','.join(str(v) for v in raw[i:i+24])+',\n' for i in range(0,len(raw),24))+'};\n')
from test_pdf_type2 import command
accent=command([0,0],21)+command([100,0,0,100,-100,0],5)+b'\x0e'
composite=command([100,600,65,194],14)
variants={
    'test_cff_cid_font':fixture(cid=True,private_count=2,fdselect=b'\0\0\0\1\0',programs=[b'\x0e',b'\x0e',BOX,CURVE]),
    'test_cff_composite_font':fixture(glyphs=5,charset=b'\0\0\1\0\x22\0\x7d\0\xab',strings=(),programs=[b'\x0e',b'\x0e',BOX,accent,composite])}
with (root/'tests/fixtures/pdf-cff.inc').open('a') as output:
    for name,raw in variants.items():
        output.write('static const unsigned char '+name+'[] = {\n'+''.join('    '+','.join(str(v) for v in raw[i:i+24])+',\n' for i in range(0,len(raw),24))+'};\n')
# This optional regeneration dependency is not needed by ordinary tests.
import base64
import io
import json
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen
fb=FontBuilder(1000,isTTF=False)
order=['.notdef','space','A']
fb.setupGlyphOrder(order)
fb.setupCharacterMap({32:'space',65:'A'})
programs={}
for name in order:
    pen=T2CharStringPen(600,None)
    if name=='A':
        pen.moveTo((0,0));pen.lineTo((400,0));pen.lineTo((400,600));pen.lineTo((0,600));pen.closePath()
    programs[name]=pen.getCharString()
fb.setupCFF('CupidShapes',{'FullName':'CupidShapes','FamilyName':'CupidShapes','Weight':'Regular'},programs,{'defaultWidthX':600})
fb.setupHorizontalMetrics({name:(600,0) for name in order})
fb.setupHorizontalHeader(ascent=800,descent=-200)
fb.setupNameTable({'familyName':'CupidShapes','styleName':'Regular'})
fb.setupOS2();fb.setupPost()
fb.font['head'].created=fb.font['head'].modified=0
fb.font.recalcTimestamp=False
output=io.BytesIO();fb.font.save(output)
(root/'tests/fixtures/pdf-cff-opentype.json').write_text(json.dumps({'provenance':'Original geometric outline font, built by generate_pdf_cff_fixtures.py','font':base64.b64encode(output.getvalue()).decode()},indent=2)+'\n')
