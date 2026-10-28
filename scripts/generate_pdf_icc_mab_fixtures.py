#!/usr/bin/env python3
"""Generate owned ICC A-to-B examples with analytic neutral raster references."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_icc import curve
from test_pdf_icc_lut import fixture, profile
from test_pdf_icc_mab import mab, clut, IDENTITY
from test_pdf_calibrated import encode


def main():
    root=Path(__file__).resolve().parents[1]
    reference=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(reference.read_text())
    neutral=curve([32896,32896])
    b_only=mab(b=[curve([32768,32768]),neutral,neutral])
    cmyk=mab(4,a=[curve()]*4,table=clut(4,16,[2,3,4,5],lambda x:(1-x[0],128/255,128/255)))
    matrix=list(IDENTITY);matrix[9]=.25
    offset=mab(m=[curve(),neutral,neutral],matrix=matrix)
    cases=(('icc-mab-b-curves.pdf',b_only,3,32768/65535*100),
           ('icc-mab-cmyk-grid.pdf',cmyk,4,50),
           ('icc-mab-matrix.pdf',offset,3,75))
    for name,table,inputs,l in cases:
        path=root/'assets/pdf/reference'/name
        path.write_bytes(fixture(profile(table,inputs),[.5]*inputs,inputs))
        value=encode(((l+16)/116)**3)
        pixels=bytes([value,value,value,255])*384
        asset=str(path.relative_to(root));rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
        rows['cases'].append({'asset':asset,'width':24,'height':16,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':1})
    reference.write_text(json.dumps(rows,indent=2)+'\n')
    print('Generated three ICC A-to-B assets and analytic neutral references')


if __name__=='__main__':main()
