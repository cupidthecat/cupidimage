#!/usr/bin/env python3
"""Generate descriptor-selected faces and an analytic PDF advance marker."""
import base64
import json
from pathlib import Path
import zlib
from test_pdf_pages import document
from test_pdf_standard_fonts import fixture


def main():
    root=Path(__file__).resolve().parents[1]
    objects={};resources=[];content=[]
    for family,flags in enumerate((32,34,33)):
        for style in range(4):
            i=family*4+style;number=5+i*2;name='CupidDescriptor'+str(i)
            weight=700 if style&1 else 400;angle=-12 if style&2 else 0
            objects[number]=f'<< /Type /Font /Subtype /TrueType /BaseFont /{name} /FontDescriptor {number+1} 0 R /Encoding /WinAnsiEncoding /FirstChar 0 /LastChar 255 /Widths ['+'600 '*256+'] >>'
            objects[number+1]=f'<< /Type /FontDescriptor /FontName /{name} /Flags {flags} /FontWeight {weight} /FontBBox [-2000 -2000 3000 3000] /ItalicAngle {angle} /Ascent 2000 /Descent -1000 /CapHeight 1000 /StemV 80 >>'
            resources.append(f'/F{i} {number} 0 R')
            x,y=10+(i//6)*150,180-(i%6)*30
            content.append(f'BT /F{i} 18 Tf 1 0 0 1 {x} {y} Tm <41422080e9> Tj ET')
    (root/'assets/pdf/descriptor-latin-families.pdf').write_bytes(document(
        '/MediaBox [0 0 300 210] /Resources << /Font << '+' '.join(resources)+' >> >>',stream='\n'.join(content),extra=objects))
    asset=root/'assets/pdf/reference/descriptor-font-widths.pdf'
    asset.write_bytes(fixture('BT /F 30 Tf 3 Tr 18 30 Td (AA) Tj /Marker 30 Tf 0 Tr (A) Tj ET',
        'CupidDescriptor','/Encoding /WinAnsiEncoding','/FontDescriptor 10 0 R /FirstChar 65 /LastChar 65 /Widths [700]',
        extra={10:b'<< /Type /FontDescriptor /FontName /CupidDescriptor /Flags 34 /FontWeight 700 /ItalicAngle -12 /FontFamily (Times New Roman) /FontBBox [-2000 -2000 3000 3000] /Ascent 2000 /Descent -1000 /CapHeight 1000 /StemV 80 >>'}))
    pixels=bytearray(b'\xff'*(160*120*4))
    for y in range(56,80):
        for x in range(80,96):pixels[(y*160+x)*4:(y*160+x)*4+3]=b'\0\0\0'
    reference=root/'tests/fixtures/pdf-image-reference.json';rows=json.loads(reference.read_text());name=str(asset.relative_to(root))
    rows['cases']=[r for r in rows['cases'] if r['asset']!=name]
    rows['cases'].append({'asset':name,'width':160,'height':120,'rgba_zlib':base64.b64encode(zlib.compress(pixels)).decode(),'tolerance':0})
    reference.write_text(json.dumps(rows,indent=2)+'\n')
    print('Generated descriptor samples and analytic advance marker')


if __name__=='__main__':main()
