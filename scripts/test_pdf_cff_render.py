#!/usr/bin/env python3
"""Compare authored CFF glyphs with explicit geometry and independent rasters."""
from pathlib import Path
import base64
import json
import subprocess
import sys
import tempfile
from test_pdf_cff import fixture as cff_font
from test_pdf_type2 import command
from test_pdf_embedded_fonts import fixture as pdf
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm

BOX=command([0,0],21)+command([400,0,0,600,-400,0],5)+b'\x0e'
CURVE=command([0,0],21)+command([400,0],5)+command([0,600,-400,0,0,-600],8)+b'\x0e'


def fixture(content,cid=False,vertical=False,encoding='/WinAnsiEncoding',font=None,extra=None,metrics=None):
    if font is None:font=cff_font(cid=cid,programs=[b'\x0e',b'\x0e',BOX,CURVE])
    if metrics is None:metrics='/DW 600' if cid else '/FirstChar 65 /LastChar 65 /Widths [600]'
    descriptor=b'<< /Type /FontDescriptor /FontName /CupidShapes /Flags 32 /FontBBox [0 0 1000 1000] /Ascent 800 /Descent -200 /CapHeight 600 /ItalicAngle 0 /StemV 80 /FontFile3 9 0 R >>'
    objects={8:descriptor,9:stream(font,'/Subtype /'+('CIDFontType0C' if cid else 'Type1C'))}
    if cid:
        objects[5]=('<< /Type /Font /Subtype /Type0 /BaseFont /CupidShapes /Encoding /Identity-'+('V' if vertical else 'H')+' /DescendantFonts [6 0 R] >>').encode()
        objects[6]=('<< /Type /Font /Subtype /CIDFontType0 /BaseFont /CupidShapes /FontDescriptor 8 0 R /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> '+metrics+' >>').encode()
    else:objects[5]=('<< /Type /Font /Subtype /Type1 /BaseFont /CupidShapes /FontDescriptor 8 0 R '+('/Encoding '+encoding if encoding else '')+' '+metrics+' >>').encode()
    objects.update(extra or {})
    return pdf(content,extra=objects)


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=refs=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-cff-render-') as tmp:
        path=Path(tmp)/'case.pdf'
        def render(content,**kwargs):
            path.write_bytes(fixture(content,**kwargs));return subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
        def same(name,content,expected,reference=True,secondary=False,**kwargs):
            nonlocal checks,refs
            actual=render(content,**kwargs);other=render(expected,**kwargs)
            assert actual.returncode==other.returncode==0,(name,actual.stderr,other.stderr)
            assert actual.stdout==other.stdout,(name,sum(a!=b for a,b in zip(actual.stdout,other.stdout)))
            checks+=1
            if '--reference' in sys.argv and reference:
                path.write_bytes(fixture(content,**kwargs));prefix=Path(tmp)/'ref'
                if secondary:
                    import fitz
                    fitz.TOOLS.set_aa_level(0)
                    with fitz.open(path) as document:
                        raster=document[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                        w,h,rgb=raster.width,raster.height,raster.samples
                else:
                    run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                    assert run.returncode==0,run.stderr
                    w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                pixels=bytes(v for i,v in enumerate(actual.stdout[12:]) if i%4!=3)
                assert (w,h)==(160,120) and pixels==rgb,(name,'reference pixels differ',sum(a!=b for a,b in zip(pixels,rgb)))
                refs+=1
        box='20 30 12 18 re f'
        same('simple','BT /F 30 Tf 20 30 Td (A) Tj ET',box)
        same('indirect-CFF-subtype','BT /F 30 Tf 20 30 Td (A) Tj ET','20 30 12 18 re f',
             extra={9:stream(cff_font(programs=[b'\x0e',b'\x0e',BOX,CURVE]),'/Subtype 10 0 R'),10:b'/Type1C'})
        same('builtin-encoding','BT /F 30 Tf 20 30 Td (A) Tj ET',box,encoding='')
        same('custom-encoding','BT /F 30 Tf 20 30 Td (X) Tj ET',box,encoding='',font=cff_font(encoding=b'\0\2\x20\x58',programs=[b'\x0e',b'\x0e',BOX,CURVE]),metrics='/FirstChar 88 /LastChar 88 /Widths [600]')
        same('name-override','BT /F 30 Tf 20 30 Td (B) Tj ET',box,encoding='<< /Differences [66 /A] >>',metrics='/FirstChar 66 /LastChar 66 /Widths [600]')
        same('mac-expert','BT /F 30 Tf 20 30 Td <47> Tj ET',box,encoding='/MacExpertEncoding',font=cff_font(charset=b'\0\0\1\0\x9e\1\x87',programs=[b'\x0e',b'\x0e',BOX,CURVE]),metrics='/FirstChar 71 /LastChar 71 /Widths [600]')
        same('program-width','BT /F 30 Tf 20 30 Td (AA) Tj ET',box+' 38 30 12 18 re f',metrics='',reference=False)
        same('pdf-width','BT /F 30 Tf 20 30 Td (AA) Tj ET',box+' 35 30 12 18 re f',metrics='/FirstChar 65 /LastChar 65 /Widths [500]')
        opentype=base64.b64decode(json.loads(Path('tests/fixtures/pdf-cff-opentype.json').read_text())['font'])
        same('opentype-cff','BT /F 30 Tf 20 30 Td (A) Tj ET',box,extra={9:stream(opentype,'/Subtype /OpenType')})
        same('cid','BT /F 30 Tf 20 30 Td <0022> Tj ET',box,cid=True)
        same('cid-two','BT /F 30 Tf 20 30 Td <00220022> Tj ET',box+' 38 30 12 18 re f',cid=True)
        same('vertical','BT /F 30 Tf 60 69 Td <0022> Tj ET','51 42 12 18 re f',cid=True,vertical=True,metrics='/DW 600 /DW2 [900 -1000]')
        for mode,operator in ((0,'f'),(1,'S'),(2,'B')):
            same('paint-mode-'+str(mode),f'1 w BT /F 30 Tf {mode} Tr 20 30 Td (A) Tj ET',f'1 w 20 30 12 18 re {operator}',reference=mode==0)
        for mode in (4,5,6,7):
            same('clip-'+str(mode),f'BT /F 30 Tf {mode} Tr 20 30 Td (A) Tj ET 1 0 0 rg 0 0 120 90 re f',('20 30 12 18 re S ' if mode in (5,6) else '')+'20 30 12 18 re W n 1 0 0 rg 0 0 120 90 re f',reference=mode==7,secondary=True)
        same('rotated','BT /F 30 Tf 0 1 -1 0 80 20 Tm (A) Tj ET','62 20 18 12 re f')
        same('scale-rise','BT /F 30 Tf 50 Tz 6 Ts 20 30 Td (A) Tj ET','20 36 6 18 re f')
        # ISO 32000-1/2 9.3.6 exempts outline-less glyphs; the comparison renderer differs.
        same('empty-outline-clip','BT /F 30 Tf 7 Tr 20 30 Td ( ) Tj ET 1 0 0 rg 0 0 120 90 re f','1 0 0 rg 0 0 120 90 re f',reference=False)
        same('zero-size-clip','BT /F 0 Tf 7 Tr 20 30 Td (A) Tj ET 1 0 0 rg 0 0 120 90 re f','q Q',reference=False)
        same('invisible','BT /F 30 Tf 3 Tr 20 30 Td (A) Tj ET','q Q')
        same('missing-cid','BT /F 30 Tf 20 30 Td <FFFF> Tj ET','q Q',cid=True)
        same('mapped-cid','BT /F 30 Tf 20 30 Td <41> Tj ET',box,cid=True,extra={
            5:b'<< /Subtype /Type0 /Encoding 10 0 R /DescendantFonts [6 0 R] >>',
            10:stream('1 begincodespacerange <00> <FF> endcodespacerange 1 begincidchar <41> 34 endcidchar')})
        same('notdef-cid','BT /F 30 Tf 20 30 Td <41> Tj ET',box,cid=True,reference=False,extra={
            5:b'<< /Subtype /Type0 /Encoding 10 0 R /DescendantFonts [6 0 R] >>',
            10:stream('1 begincodespacerange <00> <FF> endcodespacerange 1 begincidchar <41> 600 endcidchar 1 beginnotdefchar <41> 34 endnotdefchar')})
        accent=command([0,0],21)+command([100,0,0,100,-100,0],5)+b'\x0e'
        composite=command([100,600,65,194],14)
        composite_font=cff_font(glyphs=5,charset=b'\0\0\1\0\x22\0\x7d\0\xab',strings=(),programs=[b'\x0e',b'\x0e',BOX,accent,composite])
        same('composite-accent','BT /F 30 Tf 20 30 Td <C1> Tj ET',box+' 23 48 3 3 re f',font=composite_font,metrics='/FirstChar 193 /LastChar 193 /Widths [600]')
        same('font-matrix','BT /F 30 Tf 20 30 Td (A) Tj ET','2 30 18 12 re f',font=cff_font(matrix=[0,.001,-.001,0,0,0],programs=[b'\x0e',b'\x0e',BOX,CURVE]))
        same('font-matrix-offset','BT /F 30 Tf 20 30 Td (A) Tj ET','23 33 12 18 re f',font=cff_font(matrix=[.001,0,0,.001,.1,.1],programs=[b'\x0e',b'\x0e',BOX,CURVE]))
        same('reflected','BT /F 30 Tf -1 0 0 1 80 30 Tm (A) Tj ET','68 30 12 18 re f')
        same('negative-size','BT /F -30 Tf 80 60 Td (A) Tj ET','68 42 12 18 re f')
        same('ToUnicode-does-not-change-glyph','BT /F 30 Tf 20 30 Td (A) Tj ET',box,extra={
            5:b'<< /Subtype /Type1 /BaseFont /CupidShapes /Encoding /WinAnsiEncoding /FontDescriptor 8 0 R /FirstChar 65 /LastChar 65 /Widths [600] /ToUnicode 10 0 R >>',
            10:stream('1 beginbfchar <41> <0042> endbfchar')})
        # A cubic arch with an analytic symmetric horizontal span.
        arch=command([0,0],21)+command([400,0],5)+command([0,400,-400,0,0,-400],8)+b'\x0e'
        actual=render('BT /F 30 Tf 20 30 Td (B) Tj ET',encoding='<< /Differences [66 /customShape] >>',font=cff_font(programs=[b'\x0e',b'\x0e',BOX,arch]),metrics='/FirstChar 66 /LastChar 66 /Widths [600]')
        assert actual.returncode==0,actual.stderr
        # x(t)=400*(1-3*t*t+2*t*t*t), y(t)=1200*t*(1-t).
        pixels=bytearray()
        for y in range(120):
            for x in range(160):
                gx=(x+.5-1.5-20*4/3)/(.03*4/3);gy=(90-(y+.5)/(4/3)-30)/.03
                inside=False
                if 0<gx<400 and 0<gy<300:
                    import math
                    t=(1-math.sqrt(1-gy/300))/2
                    right=400*(1-3*t*t+2*t*t*t)
                    inside=400-right<gx<right
                pixels.extend((0,0,0,255) if inside else (255,255,255,255))
        assert actual.stdout[12:]==pixels,'cubic analytic pixels differ'
        checks+=1
    print(f'CFF rendering: {checks} checks passed; {refs} independent comparisons')


if __name__=='__main__':main()
