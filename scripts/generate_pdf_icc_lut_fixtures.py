#!/usr/bin/env python3
"""Generate owned ICC lookup profiles with analytic Lab/XYZ raster references."""
import base64
from decimal import Decimal as D, localcontext
import json
from pathlib import Path
import zlib
from test_pdf_icc_lut import fixture, profile, lut, lab_values
from test_pdf_calibrated import encode


def xyz_rgb(values):
    with localcontext() as context:
        context.prec=50
        m=list(map(D,['.8951','.2664','-.1614','-.7502','1.7135','.0367','.0389','-.0685','1.0296']))
        det=m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6])
        inverse=[m[4]*m[8]-m[5]*m[7],m[2]*m[7]-m[1]*m[8],m[1]*m[5]-m[2]*m[4],
                 m[5]*m[6]-m[3]*m[8],m[0]*m[8]-m[2]*m[6],m[2]*m[3]-m[0]*m[5],
                 m[3]*m[7]-m[4]*m[6],m[1]*m[6]-m[0]*m[7],m[0]*m[4]-m[1]*m[3]]
        inverse=[v/det for v in inverse]
        def mul(matrix,vector):return [sum(matrix[i*3+j]*vector[j] for j in range(3)) for i in range(3)]
        white=[D(0xf6d6)/65536,D(1),D(0xd32d)/65536]
        source=mul(m,white);target=mul(m,list(map(D,['.95047','1','1.08883'])))
        cone=mul(m,values);adapted=mul(inverse,[cone[i]*target[i]/source[i] for i in range(3)])
        rgb=mul(list(map(D,['3.2404542','-1.5371385','-.4985314','-.9692660','1.8760108','.0415560','.0556434','-.2040259','1.0572252'])),adapted)
        return [encode(float(v)) for v in rgb]


root=Path(__file__).resolve().parents[1]
reference=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(reference.read_text())
source=profile(lut(4,16,transform=lambda x:lab_values(16,100*(1-x[0]))),4)
constant=lab_values(8,25)
l=round(constant[0]*255)/255*100
xyz=[1.5,.5,.1];normalized=[v*32768/65535 for v in xyz]
encoded=[D(round(v*65535))/32768 for v in normalized]
cases=(
    ('icc-lut16-cmyk.pdf',fixture(source,[.5,0,0,0],4),[encode(((50+16)/116)**3)]*3),
    ('icc-lut8-rgb.pdf',fixture(profile(lut(3,8,transform=lambda x:constant)),[.2,.3,.4]),[encode(((l+16)/116)**3)]*3),
    ('icc-lut16-xyz-range.pdf',fixture(profile(lut(3,16,transform=lambda x:normalized),lab=False),[.2,.3,.4]),xyz_rgb(encoded)),
)
for name,pdf,rgb in cases:
    path=root/'assets/pdf/reference'/name;path.write_bytes(pdf)
    asset=str(path.relative_to(root));rows['cases']=[r for r in rows['cases'] if r['asset']!=asset]
    rows['cases'].append({'asset':asset,'width':24,'height':16,'rgba_zlib':base64.b64encode(zlib.compress(bytes(rgb+[255])*384)).decode(),'tolerance':0})
reference.write_text(json.dumps(rows,indent=2)+'\n')
print('Generated three ICC table-profile assets and analytic Lab/XYZ references')
