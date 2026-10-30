#!/usr/bin/env python3
"""Generate original triangle-mesh assets with analytic pixel expectations."""
import base64
import json
import math
from pathlib import Path
import zlib
from test_pdf_mesh import fixture, VERTICES

root=Path(__file__).resolve().parents[1]
path=root/'tests/fixtures/pdf-image-reference.json'
rows=json.loads(path.read_text())
gradient=lambda x,y:(math.floor(255*(x+.5)/24+.5),math.floor(255*(1-(y+.5)/16)+.5),0)
scalar=[(f,x,y,(x,)) for f,x,y,_ in VERTICES]
function='<< /FunctionType 2 /Domain [0 1] /C0 [0 0 0] /C1 [1 0 0] /N 2 >>'
cases=[
    ('gouraud-freeform.pdf',fixture(),gradient),
    ('gouraud-lattice.pdf',fixture(kind=5),gradient),
    ('gouraud-function.pdf',fixture(vertices=scalar,function=function),lambda x,y:(math.floor(255*((x+.5)/24)**2+.5),0,0)),
    ('gouraud-indexed.pdf',fixture(vertices=scalar,space='[/Indexed /DeviceRGB 1 <ff00000000ff>]'),lambda x,y:(math.floor(255*(1-(x+.5)/24)+.5),0,gradient(x,y)[0])),
]
for name,pdf,color in cases:
    asset='assets/pdf/reference/'+name
    (root/asset).write_bytes(pdf)
    pixels=bytes(v for y in range(16) for x in range(24) for v in (*color(x,y),255))
    rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':24,'height':16,
                         'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
path.write_text(json.dumps(rows,indent=2)+'\n')
