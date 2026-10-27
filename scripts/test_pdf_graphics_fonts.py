#!/usr/bin/env python3
"""Check ExtGState Font selection, resource scope and graphics-state restoration."""
from pathlib import Path
import subprocess
import sys
import struct
import zlib
import tempfile
from test_pdf_embedded_fonts import fixture as base_fixture, FONT
from audit_pdf_corpus import read_ppm
from test_pdf_type3 import stream


def fixture(content,states='/Use << /Font [5 0 R 30] >>',fonts='/F 5 0 R',extra=None):
    page=('<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << '+fonts+' >> /ExtGState << '+states+' >> >> >>').encode()
    objects={3:page};objects.update(extra or {})
    return base_fixture(content,extra=objects)


def compressed_fixture(content):
    objects={1:b'<< /Type /Catalog /Pages 2 0 R >>',
             2:b'<< /Type /Pages /Count 1 /Kids [3 0 R] >>',
             3:b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 120 90] /Contents 4 0 R /Resources << /ExtGState << /Use 10 0 R >> >> >>',
             4:stream(('1 0 0 1 1.125 0 cm '+content).encode()),
             8:b'<< /Type /FontDescriptor /FontName /CupidImageTestShapes /Flags 32 /FontBBox [0 0 1000 1400] /ItalicAngle 0 /Ascent 1000 /Descent -200 /CapHeight 600 /StemV 80 /FontFile2 9 0 R >>',
             9:stream(zlib.compress(FONT),'/Filter /FlateDecode /Length1 '+str(len(FONT)))}
    packed={5:b'<< /Type /Font /Subtype /TrueType /BaseFont /CupidImageTestShapes /Encoding /WinAnsiEncoding /FontDescriptor 8 0 R /FirstChar 65 /LastChar 65 /Widths [1000] >>',
            10:b'<< /Font 11 0 R >>',11:b'[5 0 R 12 0 R]',12:b'30'}
    body=bytearray();header='';indices={}
    for i,(number,value) in enumerate(packed.items()):
        header+=f'{number} {len(body)} ';indices[number]=i;body+=value+b' '
    objects[15]=stream(zlib.compress(header.encode()+body),f'/Type /ObjStm /N 4 /First {len(header)} /Filter /FlateDecode')
    out=bytearray(b'%PDF-1.7\n');offsets={}
    for number,value in sorted(objects.items()):
        offsets[number]=len(out);out+=f'{number} 0 obj\n'.encode()+value+b'\nendobj\n'
    offsets[16]=len(out);rows=bytearray()
    for number in range(17):
        if number in indices:rows+=struct.pack('>BIH',2,15,indices[number])
        elif number in offsets:rows+=struct.pack('>BIH',1,offsets[number],0)
        else:rows+=struct.pack('>BIH',0,0,65535 if number==0 else 0)
    out+=b'16 0 obj\n'+stream(rows,'/Type /XRef /Size 17 /Root 1 0 R /W [1 4 2]')+b'\nendobj\n'
    out+=f'startxref\n{offsets[16]}\n%%EOF\n'.encode()
    return bytes(out)


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=refs=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-graphics-fonts-') as tmp:
        path=Path(tmp)/'case.pdf'
        def render(content,builder=fixture,**kwargs):
            path.write_bytes(builder(content,**kwargs))
            return subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
        def same(label,content,expected,builder=fixture,**kwargs):
            nonlocal checks,refs
            actual=render(content,builder=builder,**kwargs);oracle=render(expected,builder=builder,**kwargs)
            assert actual.returncode==oracle.returncode==0,(label,actual.stderr,oracle.stderr)
            assert actual.stdout==oracle.stdout,(label,'geometry differs')
            checks+=1
            if '--reference' in sys.argv:
                path.write_bytes(builder(content,**kwargs));prefix=Path(tmp)/'reference'
                result=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                assert result.returncode==0,result.stderr
                w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                pixels=bytes(v for i,v in enumerate(actual.stdout[12:]) if i%4!=3)
                assert (w,h)==(160,120) and pixels==rgb,(label,'independent raster differs',sum(a!=b for a,b in zip(pixels,rgb)))
                refs+=1
        text='BT /Use gs 20 30 Td (A) Tj ET';box='20 30 12 18 re f'
        same('font-and-size',text,box)
        same('compressed-font-array-size',text,box,builder=compressed_fixture)
        same('font-outside-BT','/Use gs BT 20 30 Td (A) Tj ET',box)
        same('no-font-resource-name',text,box,fonts='')
        same('indirect-state',text,box,states='/Use 10 0 R',extra={10:b'<< /Font [5 0 R 30] >>'})
        same('indirect-array',text,box,states='/Use << /Font 10 0 R >>',extra={10:b'[5 0 R 30]'})
        same('indirect-size',text,box,states='/Use << /Font [5 0 R 10 0 R] >>',extra={10:b'30'})
        same('shared-program-different-size','/Use gs BT 20 30 Td (A) Tj /Small gs 30 0 Td (A) Tj ET',box+' 50 30 6 9 re f',states='/Use << /Font [5 0 R 30] >> /Small << /Font [5 0 R 15] >>')
        same('restore','/Use gs q /Small gs BT 50 30 Td (A) Tj ET Q BT 20 30 Td (A) Tj ET',box+' 50 30 6 9 re f',states='/Use << /Font [5 0 R 30] >> /Small << /Font [5 0 R 15] >>')
        same('Tf-after-gs','BT /Use gs /F 15 Tf 20 30 Td (A) Tj ET','20 30 6 9 re f')
        same('gs-after-Tf','BT /F 15 Tf /Use gs 20 30 Td (A) Tj ET',box)
        same('null-entry','BT /F 30 Tf /Use gs 20 30 Td (A) Tj ET',box,states='/Use << /Font null >>')
        same('indirect-null-entry','BT /F 30 Tf /Use gs 20 30 Td (A) Tj ET',box,states='/Use << /Font 10 0 R >>',extra={10:b'null'})
        same('zero-size',text,'q Q',states='/Use << /Font [5 0 R 0] >>')
        same('negative-size',text,'8 12 12 18 re f',states='/Use << /Font [5 0 R -30] >>')
        same('escaped-state',text,box,states='/U#73e << /Font [5 0 R 30] >>')
        same('escaped-font-resource','BT /F1 30 Tf 20 30 Td (A) Tj ET',box,fonts='/F#31 5 0 R')
        form=stream(b'BT /Use gs 50 30 Td (A) Tj ET','/Type /XObject /Subtype /Form /BBox [0 0 120 90] /Resources << /ExtGState << /Use << /Font [5 0 R 15] >> >> >>')
        page=b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /ExtGState << /Use << /Font [5 0 R 30] >> >> /XObject << /Nested 10 0 R >> >> >>'
        same('nested-form-scope','/Use gs /Nested Do BT 20 30 Td (A) Tj ET',box+' 50 30 6 9 re f',extra={3:page,10:form})
        type3=b'<< /Type /Font /Subtype /Type3 /FontBBox [0 0 400 600] /FontMatrix [.001 0 0 .001 0 0] /FirstChar 65 /LastChar 65 /Widths [600] /Encoding << /Differences [65 /A] >> /CharProcs << /A 11 0 R >> /Resources << /ExtGState << /Use << /Font [5 0 R 1000] >> >> >> >>'
        same('nested-type3-state','BT /Outer 30 Tf 20 30 Td (A) Tj ET',box,fonts='/Outer 10 0 R',extra={10:type3,11:stream(b'600 0 d0 BT /Use gs (A) Tj ET')})
        pattern=stream(b'BT /Use gs 20 30 Td (A) Tj ET','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 120 90] /XStep 120 /YStep 90 /Resources << /ExtGState << /Use << /Font [5 0 R 30] >> >> >>')
        pattern_page=b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Pattern << /P 10 0 R >> >> >>'
        pattern_named=stream(b'BT /F 30 Tf 20 30 Td (A) Tj ET','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 120 90] /XStep 120 /YStep 90 /Resources << /Font << /F 5 0 R >> >>')
        content='/Pattern cs /P scn 0 0 120 90 re f'
        a=render(content,extra={3:pattern_page,10:pattern})
        b=render(content,extra={3:pattern_page,10:pattern_named})
        assert a.returncode==b.returncode==0 and a.stdout==b.stdout,'Pattern ExtGState/Tf selection differs'
        checks+=1
        result=render(text,fonts='',extra={5:b'null'})
        assert result.returncode!=0,'Non-dictionary ExtGState font accepted'
        checks+=1
        for value in ('[]','[5 0 R]','[5 0 R 30 40]','[5 0 R (30)]','[5 0 R 1e31]','[5 0 R 30x]','[<< /Subtype /Type1 >> 30]','[999 0 R 30]','[5 0 R 10 0 R]','/F'):
            result=render(text,states='/Use << /Font '+value+' >>',extra={10:b'10 0 R'})
            assert result.returncode!=0,('invalid Font accepted',value)
            checks+=1
    print(f'Graphics-state fonts: {checks} checks passed; {refs} independent comparisons')

if __name__=='__main__':main()
