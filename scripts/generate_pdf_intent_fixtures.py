#!/usr/bin/env python3
"""Generate owned ICC intent assets using analytic PCS conversion references."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_intents import source, NAMES
from test_pdf_icc_lut import fixture
from test_pdf_calibrated import encode


def neutral(l, reference_black=False):
    l = round(l / 100 * 65280) / 65280 * 100
    y = ((l + 16) / 116) ** 3 if l > 8 else l * 27 / 24389
    if reference_black:
        y = (y - .003479) / (1 - .003479)
    return encode(y)


def main():
    root=Path(__file__).resolve().parents[1]
    reference=root/'tests/fixtures/pdf-image-reference.json'
    rows=json.loads(reference.read_text())
    raw=source()
    command='/S cs .5 .5 .5 scn '
    colors=[]
    for i,name in enumerate(NAMES):
        command+='/'+name+' ri '+str(i*4.5)+' 0 4.5 12 re f '
        value=neutral((50,25,75,50)[i],i in (1,2))
        colors.append(bytes([value,value,value,255])*6)
    cases=[('icc-intent-selection.pdf',fixture(raw,[.5]*3,content=command),b''.join(colors)*16)]
    # Overrides are local: image uses saturation, subsequent vector fill is relative.
    from test_pdf_type3 import stream
    image=stream(bytes([128]*3),'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace [/ICCBased 5 0 R] /Intent /Saturation')
    command='q 18 0 0 12 0 0 cm /Im Do Q /S cs .5 .5 .5 scn 9 0 9 12 re f'
    left=neutral(75,True);right=neutral(50)
    pixels=(bytes([left,left,left,255])*12+bytes([right,right,right,255])*12)*16
    cases.append(('icc-intent-image-local.pdf',fixture(raw,[.5]*3,content=command,extra={6:image},resources='/XObject << /Im 6 0 R >>'),pixels))
    for name,data,pixels in cases:
        path=root/'assets/pdf/reference'/name;path.write_bytes(data)
        asset=str(path.relative_to(root));rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
        rows['cases'].append({'asset':asset,'width':24,'height':16,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':1})
    reference.write_text(json.dumps(rows,indent=2)+'\n')
    print('Generated two rendering-intent assets and analytic PCS references')


if __name__=='__main__':main()
