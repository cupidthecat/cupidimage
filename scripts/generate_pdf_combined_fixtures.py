#!/usr/bin/env python3
"""Generate combined-paint regressions with analytic knockout-color references."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_combined_paint import fixture


def main():
    root=Path(__file__).resolve().parents[1]
    reference=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(reference.read_text())
    background=[51/255,179/255,102/255]
    cases=(('text-fill-stroke-shape.pdf',True,False,None),
           ('path-fill-stroke-pattern-holes.pdf',False,True,None),
           ('text-fill-stroke-mask-once.pdf',True,False,.5))
    for name,ais,pattern,mask in cases:
        path=root/'assets/pdf/reference'/name
        path.write_bytes(fixture(ais=ais,pattern=pattern,mask=mask,path=pattern))
        pixels=bytearray()
        for y in range(120):
            for x in range(160):
                px,py=x+.5,y+.5
                filled=80/3<=px<128/3 and 56<=py<80
                outer=24<=px<136/3 and 160/3<=py<248/3
                inner=88/3<px<40 and 176/3<py<232/3
                stroked=outer and not inner and (not pattern or x%8<4)
                af=.5 if filled else 0
                astroke=.5 if stroked else 0
                shape=astroke if ais else int(stroked)
                m=1 if mask is None else mask
                for i in range(3):
                    color=astroke*(i==2)+(1-shape)*af*(i==0)+(1-astroke-(1-shape)*af)*background[i]
                    pixels.append(int(255*(m*color+(1-m)*background[i])+.5+1e-9))
                pixels.append(255)
        asset=str(path.relative_to(root));rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
        rows['cases'].append({'asset':asset,'width':160,'height':120,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':1})
    reference.write_text(json.dumps(rows,indent=2)+'\n')
    print('Generated three combined-paint assets and analytic color references')


if __name__=='__main__':main()
