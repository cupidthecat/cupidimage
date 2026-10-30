#!/usr/bin/env python3
"""Generate owned wide-color and palette-range assets with analytic pixels."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_devicen import fixture, space
from test_pdf_type3 import stream
from test_pdf_patch import fixture as patch, control_points
from test_pdf_calibrated import encode

root=Path(__file__).resolve().parents[1]
reference=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(reference.read_text())
left=bytes([64,128,192]+[0]*29);right=bytes([255])*32
predicted=left+bytes((b-a)%256 for a,b in zip(left,right))
attrs='/Type /XObject /Subtype /Image /Width 2 /Height 1 /BitsPerComponent 8 /ColorSpace '+space(32)+' /Filter /FlateDecode /DecodeParms << /Predictor 2 /Colors 32 /Columns 2 /BitsPerComponent 8 >>'
image=fixture('18 0 0 12 0 0 cm /Im Do',n=32,extra={11:stream(zlib.compress(predicted),attrs)},resources='/XObject << /Im 11 0 R >>')
indexed=fixture('/I cs 1 scn 0 0 18 12 re f',n=32,spaces='/I [/Indexed '+space(32)+' 1 <'+(bytes(32)+left).hex()+'>]')
lab='[/Lab << /WhitePoint [.95047 1 1.08883] /Range [-128 127 -128 127] >>]'
patch_pdf=patch(patches=[(0,control_points(),[(0,)]*4)],space='[/Indexed '+lab+' 0 <808080>]')
gray=encode(((128/255*100+16)/116)**3)
for name,pdf,pixels in (
    ('devicen-32-predictor.pdf',image,(bytes([191,127,63,255])*12+bytes([0,0,0,255])*12)*16),
    ('devicen-32-indexed.pdf',indexed,bytes([191,127,63,255])*(24*16)),
    ('indexed-lab-patch.pdf',patch_pdf,bytes([gray,gray,gray,255])*(24*16)),
):
    path=root/'assets/pdf/reference'/name;path.write_bytes(pdf)
    asset=str(path.relative_to(root));rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':24,'height':16,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
reference.write_text(json.dumps(rows,indent=2)+'\n')
print('Generated three wide-color and palette-range assets')
