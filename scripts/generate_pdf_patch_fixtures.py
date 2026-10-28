#!/usr/bin/env python3
"""Generate original curved/folded patch assets with analytic expectations."""
import base64
import json
import math
from pathlib import Path
import zlib
from test_pdf_patch import COLORS, control_points, fixture

root=Path(__file__).resolve().parents[1]
path=root/'tests/fixtures/pdf-image-reference.json'
rows=json.loads(path.read_text())
byte=lambda value:math.floor(max(0,min(1,value))*255+.5)
quadratic=control_points(6,lambda i,j:([0,0,1/3,1][i],j/3))
tensor=control_points(7,lambda i,j:(i/3+(28/255 if i in (1,2) and j in (1,2) else 0),j/3))
def warped(x,y):
    v=1-(y+.5)/16;k=(252/255)*v*(1-v);p=(x+.5)/24
    u=2*p/(1+k+math.sqrt((1+k)**2-4*k*p))
    return byte(u),byte(v),0
fold=control_points(6,lambda i,j:(i/3,[0,1,1,0][j]))
green=[(0,0,0),(0,1,0),(0,1,0),(0,0,0)]
base=control_points(7,lambda i,j:(3*i/255,2*j/255))
second=control_points(7,lambda i,j:((9+3*i)/255,(6-2*j)/255))
solid=[(1,0,0)]*4
cases=[
    ('coons-quadratic.pdf',fixture(6,patches=[(0,quadratic,COLORS)]),lambda x,y:(byte(math.sqrt((x+.5)/24)),byte(1-(y+.5)/16),0)),
    ('tensor-interior.pdf',fixture(7,patches=[(0,tensor,COLORS)]),warped),
    ('coons-fold-priority.pdf',fixture(6,patches=[(0,fold,green)]),lambda x,y:(0,byte((1+math.sqrt(1-4*(1-(y+.5)/16)/3))/2),0) if 1-(y+.5)/16<=.75 else (255,255,255)),
    ('tensor-shared-edge.pdf',fixture(7,patches=[(0,base,solid),(2,second,solid)],flag=2,pad=1,decode=[0,255,0,255,0,1,0,1,0,1]),lambda x,y:(255,0,0) if y>=8 else (255,255,255)),
]
for name,pdf,color in cases:
    asset='assets/pdf/reference/'+name
    (root/asset).write_bytes(pdf)
    pixels=bytes(v for y in range(16) for x in range(24) for v in (*color(x,y),255))
    rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':24,'height':16,
                         'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
path.write_text(json.dumps(rows,indent=2)+'\n')
