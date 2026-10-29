#!/usr/bin/env python3
"""Check authored embedded TrueType glyphs against explicit vector geometry."""
import base64
import json
import math
import struct
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm
FONT=base64.b64decode(json.loads(Path('tests/fixtures/pdf-truetype.json').read_text())['font'])


def fixture(content,cid=False,vertical=False,extra=None,encoding='/WinAnsiEncoding',font_data=FONT,flags=32,metrics=None,font_name='CupidImageTestShapes'):
    if metrics is None:metrics='' if cid else '/FirstChar 65 /LastChar 71 /Widths [1000 1000 1000 1000 1000 1000 1000]'
    descriptor='<< /Type /FontDescriptor /FontName /CupidImageTestShapes /Flags '+str(flags)+' /FontBBox [0 0 1000 1400] /ItalicAngle 0 /Ascent 1000 /Descent -200 /CapHeight 600 /StemV 80 /FontFile2 9 0 R >>'
    objects={8:descriptor.encode(),9:stream(zlib.compress(font_data),'/Filter /FlateDecode /Length1 '+str(len(font_data)))}
    if cid:
        objects[5]=('<< /Type /Font /Subtype /Type0 /BaseFont /CupidImageTestShapes /Encoding /Identity-'+('V' if vertical else 'H')+' /DescendantFonts [6 0 R] >>').encode()
        objects[6]=('<< /Type /Font /Subtype /CIDFontType2 /BaseFont /CupidImageTestShapes /FontDescriptor 8 0 R /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap /Identity '+metrics+' >>').encode()
    else:objects[5]=('<< /Type /Font /Subtype /TrueType /BaseFont /CupidImageTestShapes /FontDescriptor 8 0 R '+('/Encoding '+encoding if encoding else '')+' '+metrics+' >>').encode()
    for key in (5,6,8):
        if key in objects:
            objects[key]=objects[key].replace(b'/CupidImageTestShapes',b'/'+font_name.encode('ascii'))
    objects.update(extra or {})
    return document('/MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> /ExtGState << /Half << /ca .5 /CA .5 >> >> >>',stream="1 0 0 1 1.125 0 cm "+content,extra=objects)


def main():
    probe=str(Path(sys.argv[1]).resolve());count=refs=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-embedded-fonts-') as tmp:
        path=Path(tmp)/'case.pdf'
        def render(content,**kwargs):
            path.write_bytes(fixture(content,**kwargs));return subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
        def same(name,content,expected,reference=True,secondary=False,allow_empty=False,**kwargs):
            nonlocal count,refs
            a=render(content,**kwargs);b=render(expected,**kwargs)
            assert a.returncode==b.returncode==0,(name,a.stderr,b.stderr)
            assert a.stdout==b.stdout,(name,'vector pixels differ',sum(x!=y for x,y in zip(a.stdout,b.stdout)))
            assert allow_empty or any(v!=255 for v in a.stdout[12:]),(name,'empty')
            count+=1
            if '--reference' in sys.argv and reference:
                path.write_bytes(fixture(content,**kwargs))
                if secondary:
                    import fitz
                    fitz.TOOLS.set_aa_level(0)
                    with fitz.open(path) as pdf:
                        raster=pdf[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False);w,h,rgb=raster.width,raster.height,raster.samples
                else:
                    prefix=Path(tmp)/'reference';r=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                    assert r.returncode==0,r.stderr;w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                actual=bytes(v for i,v in enumerate(a.stdout[12:]) if i%4!=3)
                assert (w,h)==(160,120) and actual==rgb,(name,'independent raster differs',sum(x!=y for x,y in zip(actual,rgb)))
                refs+=1
        box='20 30 12 18 re f'
        same('simple-font-box','BT /F 30 Tf 20 30 Td (A) Tj ET',box)
        from test_pdf_truetype import replace_tables
        from test_pdf_truetype_cmaps import format2,format8,format10
        cmaps=[('mixed-byte',format2(),3,1,'/WinAnsiEncoding'),
               ('mixed-word',format8(),3,10,'/WinAnsiEncoding'),
               ('trimmed-BMP',format10(65,(1,2)),3,10,'/WinAnsiEncoding'),
               ('trimmed-supplementary',format10(0x10000,(1,)),3,10,'<< /Differences [65 /u10000] >>')]
        for label,table,platform,enc,encoding in cmaps:
            cmap=struct.pack('>HHHHI',0,1,platform,enc,12)+table
            same('cmap-'+label,'BT /F 30 Tf 20 30 Td (A) Tj ET',box,
                 font_data=replace_tables(FONT,{b'cmap':cmap}),encoding=encoding,secondary=label!='mixed-byte')
        from test_pdf_truetype import tables
        post_names=json.loads(Path('tests/fixtures/pdf-post-names.json').read_text())['names']
        _,maxp_at,_=tables(FONT)[b'maxp'];glyph_count=struct.unpack_from('>H',FONT,maxp_at+4)[0]
        ids=[0,post_names.index('Delta')]+[0]*(glyph_count-2)
        post=struct.pack('>I',0x20000)+bytes(28)+struct.pack('>H',glyph_count)+struct.pack('>'+str(glyph_count)+'H',*ids)
        same('post-standard-extended','BT /F 30 Tf 20 30 Td (A) Tj ET',box,
             font_data=replace_tables(FONT,{b'post':post,b'cmap':b'\0\0\0\0'}),encoding='<< /Differences [65 /Delta] >>',secondary=True)
        post=struct.pack('>I',0x25000)+bytes(28)+struct.pack('>H',glyph_count)+bytes([0,post_names.index('A')-1]+[0]*(glyph_count-2))
        same('post-legacy-offset','BT /F 30 Tf 20 30 Td (A) Tj ET',box,
             font_data=replace_tables(FONT,{b'post':post,b'cmap':b'\0\0\0\0'}),secondary=True)
        same('indirect-TrueType-subtype','BT /F 30 Tf 20 30 Td (A) Tj ET',box,
             extra={5:b'<< /Type /Font /Subtype 10 0 R /BaseFont /CupidImageTestShapes /FontDescriptor 8 0 R /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 65 /Widths [1000] >>',10:b'/TrueType'})
        same('indirect-CID-subtypes','BT /F 30 Tf 20 30 Td <0001> Tj ET',box,cid=True,
             extra={5:b'<< /Type /Font /Subtype 10 0 R /BaseFont /CupidImageTestShapes /Encoding /Identity-H /DescendantFonts [6 0 R] >>',
                    6:b'<< /Type /Font /Subtype 11 0 R /BaseFont /CupidImageTestShapes /FontDescriptor 8 0 R /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap /Identity /DW 1000 >>',10:b'/Type0',11:b'/CIDFontType2'})
        same('CID-box','BT /F 30 Tf 20 30 Td <0001> Tj ET',box,cid=True)
        same('CID-without-Unicode','BT /F 30 Tf 20 30 Td <00010001> Tj ET',box+' 50 30 12 18 re f',cid=True)
        same('hmtx-advance','BT /F 30 Tf 20 30 Td (AA) Tj ET',box+' 50 30 12 18 re f',metrics='',reference=False)
        same('PDF-width-overrides-hmtx','BT /F 30 Tf 20 30 Td (AA) Tj ET',box+' 35 30 12 18 re f',metrics='/FirstChar 65 /LastChar 65 /Widths [500]')
        hole='20 30 18 18 re 24.5 34.5 9 9 re f*'
        same('hole','BT /F 30 Tf 20 30 Td (B) Tj ET',hole)
        same('composite-transform','BT /F 30 Tf 20 30 Td (D) Tj ET',box+' 35 30 m 39.5 43.5 l 45.5 43.5 l 41 30 l h f',reference=False)
        same('composite-point-attachment','BT /F 30 Tf 20 30 Td (E) Tj ET',box+' 32 48 12 18 re f')
        actual=render('BT /F 30 Tf 20 30 Td (C) Tj ET')
        assert actual.returncode==0,actual.stderr
        ratio=struct.unpack('f',struct.pack('f',96/72))[0]
        def curved_pixel(x,y):
            gx=((x+.5)/ratio-21.125)/.03;gy=(60-(y+.5)/ratio)/.03
            if gx<0 or gx>600 or gy<0:return (255,255,255,255)
            u=min(gx,600-gx)
            if u<150:
                t=math.sqrt(u/150);top=1200*t-500*t*t
            else:
                t=(gx-150)/300;top=700+200*t-200*t*t
            return (0,0,0,255) if gy<top else (255,255,255,255)
        analytic=bytes(v for y in range(120) for x in range(160) for v in curved_pixel(x,y))
        assert actual.stdout[12:]==analytic,'quadratic analytic pixels differ'
        count+=1
        for char in ('F','G'):
            same('implied-contour-start',f'BT /F 30 Tf 20 30 Td ({char}) Tj ET','BT /F 30 Tf 20 30 Td (C) Tj ET',reference=False)
        for mode,operator in ((0,'f'),(1,'S'),(2,'B')):
            same('paint-mode',f'1 w BT /F 30 Tf {mode} Tr 20 30 Td (A) Tj ET',f'1 w 20 30 12 18 re {operator}',reference=mode==0)
        for mode in (4,5,6,7):
            # Clip-only paint is compared with an explicit path clip and fill.
            if mode==7:same('clip-outline','BT /F 30 Tf 7 Tr 20 30 Td (A) Tj ET 1 0 0 rg 0 0 120 90 re f','20 30 12 18 re W n 1 0 0 rg 0 0 120 90 re f',secondary=True)
            else:same('clip-and-paint',f'BT /F 30 Tf {mode} Tr 20 30 Td (A) Tj ET',f'BT /F 30 Tf {mode-4} Tr 20 30 Td (A) Tj ET',reference=False)
        same('text-rise','BT /F 30 Tf 5 Ts 20 30 Td (A) Tj ET','20 35 12 18 re f')
        same('horizontal-scale','BT /F 30 Tf 50 Tz 20 30 Td (A) Tj ET','20 30 6 18 re f')
        same('vertical-origin','BT /F 30 Tf 60 70 Td <0001> Tj ET','45 43.6 12 18 re f',cid=True,vertical=True,reference=False)
        same('differences-custom-name','BT /F 30 Tf 20 30 Td (Z) Tj ET',box,encoding='<< /BaseEncoding /WinAnsiEncoding /Differences [90 /box] >>')
        same('differences-Unicode-name','BT /F 30 Tf 20 30 Td (Z) Tj ET',box,encoding='<< /Differences [90 /uni0041] >>',reference=False)
        same('ToUnicode-is-not-glyph-selection','BT /F 30 Tf 20 30 Td (A) Tj ET',box,
             extra={5:b'<< /Type /Font /Subtype /TrueType /BaseFont /CupidImageTestShapes /Encoding /WinAnsiEncoding /FontDescriptor 8 0 R /ToUnicode 10 0 R >>',10:stream('1 beginbfchar <41> <0042> endbfchar')})
        same('WinAnsi-Euro','BT /F 30 Tf 20 30 Td <80> Tj ET',box)
        same('MacRoman-currency','BT /F 30 Tf 20 30 Td <DB> Tj ET',hole,encoding='/MacRomanEncoding')
        same('non-BMP-glyph-name','BT /F 30 Tf 20 30 Td (Z) Tj ET',box,encoding='<< /Differences [90 /u10000] >>',reference=False)
        same('symbolic-ignores-Encoding','BT /F 30 Tf 20 30 Td (A) Tj ET',box,flags=4,encoding='<< /Differences [65 /hole] >>',reference=False)
        same('single-byte-despite-ToUnicode','BT /F 30 Tf 20 30 Td (AB) Tj ET',box+' 50 30 18 18 re 54.5 34.5 9 9 re f*',
             extra={5:b'<< /Subtype /TrueType /Encoding /WinAnsiEncoding /FontDescriptor 8 0 R /FirstChar 65 /LastChar 66 /Widths [1000 1000] /ToUnicode 10 0 R >>',10:stream('1 beginbfchar <4142> <0043> endbfchar')},reference=False)
        same('CID-to-glyph-map','BT /F 30 Tf 20 30 Td <0001> Tj ET',hole,cid=True,
             extra={6:b'<< /Subtype /CIDFontType2 /FontDescriptor 8 0 R /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap 10 0 R >>',10:stream(zlib.compress(bytes([0,0,0,2,0,1])),'/Filter /FlateDecode')})
        same('OpenType-glyf-program','BT /F 30 Tf 20 30 Td (A) Tj ET',box,
             extra={8:b'<< /Type /FontDescriptor /FontName /CupidImageTestShapes /Flags 32 /FontBBox [0 0 1000 1400] /ItalicAngle 0 /Ascent 1000 /Descent -200 /CapHeight 600 /StemV 80 /FontFile3 9 0 R >>',9:stream(zlib.compress(FONT),'/Filter /FlateDecode /Subtype /OpenType')})
        for matrix in ('0 1 -1 0 80 10','1 .25 .2 1 10 10','-1 0 0 1 80 0','1 0 0 -1 0 80'):
            same('affine-outline',f'q {matrix} cm BT /F 30 Tf 20 30 Td (A) Tj ET Q',f'q {matrix} cm {box} Q',reference=False)
        canceled=render('BT /F 30 Tf 7 Tr 20 30 Td (A) Tj -1 0 0 1 32 30 Tm (A) Tj ET 0 0 120 90 re f')
        assert canceled.returncode==0 and all(v==255 for v in canceled.stdout[12:]);count+=1
        same('indirect-encoding-values','BT /F 30 Tf 20 30 Td (Z) Tj ET',box,
             encoding='<< /BaseEncoding 10 0 R /Differences 11 0 R >>',extra={10:b'/WinAnsiEncoding',11:b'[90 /box]'})
        same('indirect-CID-identity','BT /F 30 Tf 20 30 Td <0001> Tj ET',box,cid=True,
             extra={6:b'<< /Subtype /CIDFontType2 /FontDescriptor 8 0 R /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap 10 0 R >>',10:b'/Identity'})
        same('missing-glyph','BT /F 30 Tf 20 30 Td <FFFF> Tj ET','q Q',cid=True,allow_empty=True)
        same('hole-clipping','BT /F 30 Tf 7 Tr 20 30 Td (B) Tj ET 1 0 0 rg 0 0 120 90 re f',
             '20 30 18 18 re 24.5 34.5 9 9 re W* n 1 0 0 rg 0 0 120 90 re f',reference=False)
        same('source-control-byte-glyph','BT /F 30 Tf 20 30 Td <01> Tj ET',box,encoding='<< /Differences [1 /box] >>',reference=False)
        same('space-glyph-and-word-spacing','BT /F 30 Tf 5 Tw 20 30 Td ( A) Tj ET',box+' 55 30 12 18 re f',metrics='',reference=False)
        from test_pdf_truetype import tables
        locations=tables(FONT);_,loca,_=locations[b'loca'];_,glyf,_=locations[b'glyf']
        at=glyf+struct.unpack_from('>H',FONT,loca+8)[0]*2
        broken=bytearray(FONT);struct.pack_into('>H',broken,at+12,4)
        bad=render('BT /F 30 Tf 20 30 Td (D) Tj ET',font_data=bytes(broken))
        assert bad.returncode==3,bad.stderr;count+=1
        same('alpha-union','/Half gs BT /F 30 Tf 20 30 Td (A) Tj ET','/Half gs '+box,reference=False)
        same('negative-font-size','BT /F -30 Tf 50 60 Td (A) Tj ET','38 42 12 18 re f',reference=False)
    print(f'Embedded TrueType: {count} checks passed; {refs} independent comparisons')
if __name__=='__main__':main()
