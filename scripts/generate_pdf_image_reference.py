#!/usr/bin/env python3
"""Generate owned PDF image fixtures and analytic RGBA expectations."""
import base64
import json
import math
from pathlib import Path
import zlib
from test_pdf_images import fixture, image_stream
from test_pdf_packed_images import encode, pack
from test_pdf_lzw import literals
from test_pdf_pages import document
from test_pdf_object_syntax import pdf as syntax_pdf, stream as syntax_stream
from test_pdf_shading import shading_pdf
from test_pdf_sampled_functions import sampled
from test_pdf_calculator import calculator

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
save_document('compact-objects.pdf', syntax_pdf(ws=b'\t', separator=b' ', compact=True), lambda x,y:(255,0,0))
compressed_objects = [b'<< /Type /Catalog /Pages 2 0 R >>', b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
                      b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R >>']
object_offsets = [0, len(compressed_objects[0])+1, len(compressed_objects[0])+len(compressed_objects[1])+2]
object_header = ' '.join(f'{i+1} {offset}' for i,offset in enumerate(object_offsets)).encode()+b' '
object_data = object_header+b' '.join(compressed_objects)
save_document('lzw-object-stream.pdf', syntax_pdf({
    4: syntax_stream(b'1 0 0 rg 0 0 18 12 re f'),
    5: syntax_stream(literals(object_data, 1), f'/Type /ObjStm /N 3 /First {len(object_header)} /Filter /LZWDecode')},
    compressed=[1,2,3]), lambda x,y:(255,0,0))
gradient_function = '<< /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >>'
gradient_rgb = lambda t: (int(255*(1-t)+.5), 0, int(255*t+.5))
save_document('axial-shading.pdf', shading_pdf(
    '<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Function 6 0 R >>', gradient_function),
    lambda x,y:gradient_rgb((x+.5)/24))
save_document('radial-shading.pdf', shading_pdf(
    '<< /ShadingType 3 /ColorSpace /DeviceRGB /Coords [9 6 0 9 6 6] /Extend [true true] /Function 6 0 R >>', gradient_function),
    lambda x,y:gradient_rgb(min(1,math.hypot((x+.5)*.75-9,12-(y+.5)*.75-6)/6)))
save_document('exponential-shading.pdf', shading_pdf(
    '<< /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 18 0] /Function 6 0 R >>',
    '<< /FunctionType 2 /Domain [0 1] /N 2 >>'),lambda x,y:(int(((x+.5)/24)**2*255+.5),)*3)
axial = '<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Function 6 0 R >>'
save_document('stitching-function.pdf', shading_pdf(axial,
    '<< /FunctionType 3 /Domain [0 1] /Functions [7 0 R 7 0 R] /Bounds [.5] /Encode [0 1 1 0] >>',
    extra={7:gradient_function}),lambda x,y:gradient_rgb(2*(x+.5)/24 if x<12 else 2*(1-(x+.5)/24)))
component_functions = '[ << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >> '+\
    '<< /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [0] /N 1 >> '+\
    '<< /FunctionType 2 /Domain [0 1] /N 1 >> ]'
save_document('component-functions.pdf',shading_pdf(axial,component_functions),lambda x,y:gradient_rgb((x+.5)/24))
save_document('sampled-32bit-function.pdf',shading_pdf(axial.replace('DeviceRGB','DeviceGray'),
    sampled((0,1),32,components=1,attrs='/Decode [0 4294967295]')),lambda x,y:(int((x+.5)/24*255+.5),)*3)
save_document('function-based-shading.pdf',shading_pdf(
    '<< /ShadingType 1 /ColorSpace /DeviceRGB /Matrix [18 0 0 12 0 0] /Function 6 0 R >>',
    sampled((255,0,0,0,255,0,0,0,255,255,255,255),size=(2,2))),
    lambda x,y:tuple(int(c*255+.5) for c in ((1-(x+.5)/24)*(y+.5)/16+((x+.5)/24)*(1-(y+.5)/16),(x+.5)/24,1-(y+.5)/16)))
save_document('calculator-gradient.pdf',shading_pdf(axial,
    calculator('{ 1 exch sub dup 0 exch 1 exch sub }',filtered=True)),lambda x,y:gradient_rgb((x+.5)/24))
save_document('calculator-conditional.pdf',shading_pdf(axial,
    calculator('{ .5 lt { 1 0 0 } { 0 0 1 } ifelse }')),lambda x,y:(255,0,0)if x<12 else(0,0,255))
save_document('indexed-vector-fill.pdf', document(
    '/MediaBox [0 0 18 12] /Resources << /ColorSpace << /Palette [/Indexed /DeviceRGB 2 <ff000000ff000000ff>] >> >>',
    stream='/Palette cs 0 0 6 12 re f 1 sc 6 0 6 12 re f 2 scn 12 0 6 12 re f'),
    lambda x,y:((255,0,0),(0,255,0),(0,0,255))[x//8])
from test_pdf_cff import fixture as cff_program
from test_pdf_cff_render import BOX as cff_box
for cid, name in ((False, 'embedded-cff-simple.pdf'), (True, 'embedded-cff-cid.pdf')):
    program = cff_program(cid=cid, programs=[b'\x0e', b'\x0e', cff_box, b'\x0e'])
    descriptor = '<< /Type /FontDescriptor /FontName /CupidShapes /Flags 32 /FontBBox [0 0 400 600] /Ascent 600 /Descent 0 /CapHeight 600 /ItalicAngle 0 /StemV 80 /FontFile3 9 0 R >>'
    font = '<< /Type /Font /Subtype /Type1 /BaseFont /CupidShapes /Encoding /WinAnsiEncoding /FontDescriptor 8 0 R /FirstChar 65 /LastChar 65 /Widths [600] >>'
    extra = {5:font.encode(),8:descriptor.encode(),9:syntax_stream(program,'/Subtype /'+('CIDFontType0C' if cid else 'Type1C'))}
    if cid:
        extra[5] = b'<< /Type /Font /Subtype /Type0 /BaseFont /CupidShapes /Encoding /Identity-H /DescendantFonts [6 0 R] >>'
        extra[6] = b'<< /Type /Font /Subtype /CIDFontType0 /BaseFont /CupidShapes /FontDescriptor 8 0 R /DW 600 /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> >>'
    save_document(name,document('/MediaBox [0 0 18 12] /Resources << /Font << /F 5 0 R >> >>',
        stream='BT /F 15 Tf 3 1.5 Td '+('<0022>' if cid else '(A)')+' Tj ET',extra=extra),
        lambda x,y:(0,0,0) if 4<=x<12 and 2<=y<14 else (255,255,255))
from test_pdf_type1 import fixture as type1_program
from test_pdf_type1_charstrings import BOX as type1_box, WIDTH as type1_width
for compressed, name in ((False,'embedded-type1.pdf'),(True,'embedded-type1-filtered.pdf')):
    program,length1,length2=type1_program(glyphs=[('.notdef',type1_width+b'\x0e'),('A',type1_box)])
    attrs=f'/Length1 {length1} /Length2 {length2} /Length3 0'
    if compressed:
        program=zlib.compress(program);attrs+=' /Filter /FlateDecode'
    extra={5:b'<< /Type /Font /Subtype /Type1 /BaseFont /CupidShapes /Encoding /WinAnsiEncoding /FontDescriptor 8 0 R /FirstChar 65 /LastChar 65 /Widths [600] >>',
           8:b'<< /Type /FontDescriptor /FontName /CupidShapes /Flags 32 /FontBBox [0 0 400 600] /Ascent 600 /Descent 0 /CapHeight 600 /ItalicAngle 0 /StemV 80 /FontFile 9 0 R >>',
           9:syntax_stream(program,attrs)}
    save_document(name,document('/MediaBox [0 0 18 12] /Resources << /Font << /F 5 0 R >> >>',
        stream='BT /F 15 Tf 3 1.5 Td (A) Tj ET',extra=extra),
        lambda x,y:(0,0,0) if 4<=x<12 and 2<=y<14 else (255,255,255))
(ROOT/'tests/fixtures/pdf-image-reference.json').write_text(json.dumps({'provenance':'Owned synthetic pixels; expected RGBA is calculated from the source samples, filter and predictor rules, Decode mappings, and stencil, color-key, explicit, soft masks, and RGB compositing.','cases':rows},indent=2)+'\n')
print('Generated',len(rows),'PDF image reference assets')
