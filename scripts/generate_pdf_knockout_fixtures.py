#!/usr/bin/env python3
"""Generate analytic fixtures distinguishing knockout shape from opacity."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_groups import fixture

root=Path(__file__).resolve().parents[1]
path=root/'tests/fixtures/pdf-image-reference.json'
rows=json.loads(path.read_text())
for ais,color in ((False,(51,51,255)),(True,(51,0,204))):
    asset='assets/pdf/reference/knockout-'+('shape' if ais else 'opacity')+'.pdf'
    body='1 0 0 rg 0 0 18 12 re f /B gs 0 0 1 rg 0 0 18 12 re f'
    pdf=fixture(program=body,group='<< /S /Transparency /I true /K true >>',
                attrs='/Resources << /ExtGState << /B << /ca .8 /AIS '+str(ais).lower()+' >> >> >>')
    (root/asset).write_bytes(pdf)
    pixels=bytes((*color,255))*(24*16)
    rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':24,'height':16,
                         'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
path.write_text(json.dumps(rows,indent=2)+'\n')
