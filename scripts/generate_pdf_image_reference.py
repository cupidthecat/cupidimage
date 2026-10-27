#!/usr/bin/env python3
"""Generate owned PDF image fixtures and analytic RGBA expectations."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_images import fixture, image_stream
from test_pdf_packed_images import encode, pack
from test_pdf_lzw import literals
from test_pdf_pages import document

ROOT=Path(__file__).resolve().parents[1]
rows=[]

def save_document(name, data, expected):
    path = ROOT / 'assets/pdf/reference' / name
    path.write_bytes(data.replace(b'%PDF-1.4', b'%PDF-1.7'))
    pixels = bytes(c for y in range(16) for x in range(24) for c in (*expected(x,y),255))
    rows.append({'asset':str(path.relative_to(ROOT)), 'width':24, 'height':16,
                 'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(), 'tolerance':0})


def save(name, payload, attrs, expected, content='18 0 0 12 0 0 cm /Im0 Do', extra=None):
    save_document(name, fixture(payload, attrs, content=content, extra=extra), expected)


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
mask_attrs = '/Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceGray'
alpha_values = [0, 85, 170, 255]
save('soft-mask.pdf', bytes((255, 0, 0)),
     '/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB /SMask 8 0 R',
     lambda x, y: tuple(int(alpha_values[(y//8)*2+x//12]/255*c + (1-alpha_values[(y//8)*2+x//12]/255)*128 + .5) for c in (255, 0, 0)),
     extra={8: image_stream(bytes(alpha_values), mask_attrs)})
save('soft-mask-matte.pdf', bytes((255, 127, 127)),
     '/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB /SMask 8 0 R',
     lambda x, y: (192, 64, 64),
     extra={8: image_stream(b'\x80', '/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray /Matte [1 1 1]')})
save('explicit-image-mask.pdf', bytes((0, 255, 0)),
     '/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB /Mask 8 0 R',
     lambda x, y: (0, 255, 0) if x//12 == y//8 else (128, 128, 128),
     extra={8: image_stream(b'\x40\x80', '/Width 2 /Height 2 /ImageMask true')})
rows_rgb = [[255, 0, 0, 0, 255, 0, 0, 0, 255], [0, 255, 255, 255, 0, 255, 255, 255, 0]]
predicted = encode(rows_rgb, 3, 8, 15)
params = '<< /Predictor 15 /Colors 3 /Columns 3 /BitsPerComponent 8 >>'
image_attrs = '/Width 3 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB'
save('lzw-predictor.pdf', literals(predicted, 1),
     image_attrs + ' /Filter /LZWDecode /DecodeParms ' + params,
     lambda x, y: colors[(y//8)*3+x//8])
save('ascii85-flate-predictor.pdf', base64.a85encode(zlib.compress(predicted)) + b'~>',
     image_attrs + ' /Filter [/ASCII85Decode /FlateDecode] /DecodeParms [null '+params+']',
     lambda x, y: colors[(y//8)*3+x//8])
save('indexed-soft-mask.pdf', bytes((0, 1, 2, 2, 1, 0)),
     '/Width 3 /Height 2 /BitsPerComponent 8 /ColorSpace 8 0 R /SMask 9 0 R',
     lambda x,y: (128,128,128) if y < 8 else ((0,0,255),(0,255,0),(255,0,0))[x//8],
     extra={8:'[/Indexed /DeviceRGB 2 <ff000000ff000000ff>]',
            9:image_stream(b'\x00\xff', '/Width 1 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceGray')})
page = '/MediaBox [0 0 18 12] /Resources << /ExtGState << /G 8 0 R >> >>'
save_document('blend-multiply.pdf', document(page,
    stream='0.5 g 0 0 18 12 re f /G gs 1 0 0 rg 0 0 18 12 re f',
    extra={8:'<< /ca .5 /BM /Multiply >>'}), lambda x,y:(128,64,64))
save_document('blend-screen.pdf', document(page,
    stream='0 0 1 rg 0 0 18 12 re f /G gs 1 0 0 rg 0 0 18 12 re f',
    extra={8:'<< /ca .5 /BM /Screen >>'}), lambda x,y:(128,0,255))
save_document('fill-stroke-opacity.pdf', document(page,
    stream='/G gs 1 0 0 rg 0 0 1 RG 3 w 0 0 18 12 re B',
    extra={8:'<< /ca .5 /CA .5 >>'}),
    lambda x,y:(128,128,255) if x<2 or x>=22 or y<2 or y>=14 else (255,128,128))
(ROOT/'tests/fixtures/pdf-image-reference.json').write_text(json.dumps({'provenance':'Owned synthetic pixels; expected RGBA is calculated from the source samples, filter and predictor rules, Decode mappings, and stencil, color-key, explicit, soft masks, and RGB compositing.','cases':rows},indent=2)+'\n')
print('Generated',len(rows),'PDF image reference assets')
