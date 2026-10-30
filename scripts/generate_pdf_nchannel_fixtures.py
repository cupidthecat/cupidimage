#!/usr/bin/env python3
"""Generate process-only NChannel assets with consistent alternates and analytic pixels."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_nchannel import fixture, paint, space
from test_pdf_type3 import stream

root=Path(__file__).resolve().parents[1]
reference=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(reference.read_text())
cmyk=space(('Yellow','Cyan'),'/DeviceCMYK',('Cyan','Magenta','Yellow','Black'),alternate='/DeviceCMYK')
transform=stream('{ exch 0 exch 0 }','/FunctionType 4 /Domain [0 1 0 1] /Range [0 1 0 1 0 1 0 1]')
cases=(
    ('nchannel-process-rgb.pdf',fixture(paint([64/255,128/255,192/255]),identity=True),[64,128,192,255]),
    ('nchannel-process-gray.pdf',fixture(paint([.5]),space(('Tone',),'/DeviceGray',alternate='/DeviceGray'),inputs=1,outputs=1,identity=True),[128,128,128,255]),
    ('nchannel-process-cmyk-subset.pdf',fixture(paint([1,1]),cmyk,inputs=2,outputs=4,extra={10:transform}),[0,255,0,255]),
)
for name,pdf,color in cases:
    path=root/'assets/pdf/reference'/name;path.write_bytes(pdf)
    asset=str(path.relative_to(root));rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':24,'height':16,'rgba_zlib':base64.b64encode(zlib.compress(bytes(color)*384)).decode(),'tolerance':0})
reference.write_text(json.dumps(rows,indent=2)+'\n')
print('Generated three process NChannel assets with analytic references')
