#!/usr/bin/env python3
"""Generate owned PDF image fixtures and analytic RGBA expectations."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_images import fixture
from test_pdf_packed_images import encode, pack

ROOT=Path(__file__).resolve().parents[1]
rows=[]

def save(name,payload,attrs,expected,content='18 0 0 12 0 0 cm /Im0 Do'):
    path=ROOT/'assets/pdf/reference'/name
    path.write_bytes(fixture(payload,attrs,content=content).replace(b'%PDF-1.4',b'%PDF-1.7'))
    pixels=bytes(c for y in range(16)for x in range(24)for c in (*expected(x,y),255))
    rows.append({'asset':str(path.relative_to(ROOT)),'width':24,'height':16,
                 'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})

values=[0,1,0,1,0,1]
save('packed-gray-1bit.pdf',pack(values[:3],1)+pack(values[3:],1),
     '/Width 3 /Height 2 /BitsPerComponent 1 /ColorSpace /DeviceGray',
     lambda x,y:(values[(y//8)*3+x//8]*255,)*3)
colors=[(255,0,0),(0,255,0),(0,0,255),(0,255,255),(255,0,255),(255,255,0)]
values=[c//17 for pixel in colors for c in pixel]
encoded=encode([values[:9],values[9:]],3,4,15)
save('packed-rgb-4bit-predictor.pdf',zlib.compress(encoded),
     '/Width 3 /Height 2 /BitsPerComponent 4 /ColorSpace /DeviceRGB /Filter /FlateDecode /DecodeParms << /Predictor 15 /Columns 3 /Colors 3 /BitsPerComponent 4 >>',
     lambda x,y:colors[(y//8)*3+x//8])
values16=[0,65535,21845,43690,32767,65535]
save('gray-16bit-tiff-predictor.pdf',zlib.compress(encode([values16[:3],values16[3:]],1,16,2)),
     '/Width 3 /Height 2 /BitsPerComponent 16 /ColorSpace /DeviceGray /Filter /FlateDecode /DecodeParms << /Predictor 2 /Columns 3 /Colors 1 /BitsPerComponent 16 >>',
     lambda x,y:(int(values16[(y//8)*3+x//8]*255/65535+.5),)*3)
save('stencil-mask.pdf',b'\x40\xa0','/Width 3 /Height 2 /ImageMask true',
     lambda x,y:(0,255,0)if((y//8)*3+x//8)%2==0 else(128,128,128),
     '0 1 0 rg 18 0 0 12 0 0 cm /Im0 Do')
save('color-key-mask.pdf',bytes(c for pixel in colors for c in pixel),
     '/Width 3 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB /Mask [255 255 0 0 0 0]',
     lambda x,y:(128,128,128)if(y//8)*3+x//8==0 else colors[(y//8)*3+x//8])
(ROOT/'tests/fixtures/pdf-image-reference.json').write_text(json.dumps({'provenance':'Owned synthetic pixels; expected RGBA is calculated from the source samples, Decode rules, and stencil/color-key masks.','cases':rows},indent=2)+'\n')
print('Generated',len(rows),'PDF image reference assets')
