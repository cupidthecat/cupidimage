#!/usr/bin/env python3
"""Compare embedded binary Type 1 glyphs with geometric PDF expectations."""
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path
from test_pdf_type1 import fixture as font_program
from test_pdf_type1_charstrings import command, WIDTH, BOX
from test_pdf_embedded_fonts import fixture as pdf
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm
CURVE=WIDTH+command([0,0],21)+command([400],6)+command([0,400,-400,0,0,-400],8)+b'\x09\x0e'


def fixture(content,encoding='/WinAnsiEncoding',metrics='/FirstChar 65 /LastChar 66 /Widths [600 600]',glyphs=None,compressed=False,extra=None,subtype="Type1",**kwargs):
    if glyphs is None:glyphs=[('.notdef',WIDTH+b'\x0e'),('space',WIDTH+b'\x0e'),('A',BOX),('B',CURVE)]
    raw,l1,l2=font_program(glyphs=glyphs,**kwargs)
    attributes=f'/Length1 {l1} /Length2 {l2} /Length3 0'
    if compressed:raw=zlib.compress(raw);attributes+=' /Filter /FlateDecode'
    objects={5:('<< /Type /Font /Subtype /'+subtype+' /BaseFont /CupidShapes /FontDescriptor 8 0 R '+('/Encoding '+encoding if encoding else '')+' '+metrics+' >>').encode(),
             8:b'<< /Type /FontDescriptor /FontName /CupidShapes /Flags 32 /FontBBox [0 -200 1000 1000] /Ascent 800 /Descent -200 /ItalicAngle 0 /CapHeight 600 /StemV 80 /FontFile 9 0 R >>',9:stream(raw,attributes)}
    objects.update(extra or {})
    return pdf(content,extra=objects)


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=refs=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-type1-render-') as tmp:
        path=Path(tmp)/'case.pdf'
        def render(content,**kwargs):
            path.write_bytes(fixture(content,**kwargs));return subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
        def same(name,content,expected,reference=True,secondary=False,**kwargs):
            nonlocal checks,refs
            a=render(content,**kwargs);b=render(expected,**kwargs)
            assert a.returncode==b.returncode==0,(name,a.stderr,b.stderr)
            assert a.stdout==b.stdout,(name,sum(x!=y for x,y in zip(a.stdout,b.stdout)))
            checks+=1
            if '--reference' in sys.argv and reference:
                path.write_bytes(fixture(content,**kwargs))
                if secondary:
                    import fitz
                    fitz.TOOLS.set_aa_level(0)
                    with fitz.open(path) as doc:
                        pix=doc[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False);w,h,rgb=pix.width,pix.height,pix.samples
                else:
                    prefix=Path(tmp)/'ref';r=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                    assert r.returncode==0,r.stderr;w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                pixels=bytes(v for i,v in enumerate(a.stdout[12:]) if i%4!=3)
                assert (w,h)==(160,120) and pixels==rgb,(name,'reference pixels differ',sum(x!=y for x,y in zip(pixels,rgb)))
                refs+=1
        text='BT /F 30 Tf 20 30 Td (A) Tj ET';box='20 30 12 18 re f'
        same('simple',text,box)
        same('multiple-master-snapshot',text,box,subtype='MMType1')
        same('indirect-subtype',text,box,extra={5:b'<< /Type /Font /Subtype 10 0 R /BaseFont /CupidShapes /FontDescriptor 8 0 R /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 65 /Widths [600] >>',10:b'/Type1'})
        same('binary-newline-after-CR',text,box,separator=b'\r',iv_first=26)
        same('unencrypted-outer-program',text,box,outer_encrypted=False,reference=False)
        same('filtered',text,box,compressed=True)
        for iv in (-1,0,1,16):same('lenIV-'+str(iv),text,box,len_iv=iv)
        same('builtin-encoding',text,box,encoding='')
        same('encoding-override','BT /F 30 Tf 20 30 Td (C) Tj ET',box,encoding='<< /Differences [67 /A] >>',metrics='/FirstChar 67 /LastChar 67 /Widths [600]')
        same('program-width','BT /F 30 Tf 20 30 Td (AA) Tj ET',box+' 38 30 12 18 re f',metrics='',reference=False)
        same('pdf-width','BT /F 30 Tf 20 30 Td (AA) Tj ET',box+' 35 30 12 18 re f',metrics='/FirstChar 65 /LastChar 65 /Widths [500]')
        for mode,op in ((0,'f'),(1,'S'),(2,'B')):
            same('paint-'+str(mode),f'1 w BT /F 30 Tf {mode} Tr 20 30 Td (A) Tj ET',f'1 w 20 30 12 18 re {op}',reference=mode==0)
        for mode in (4,5,6,7):
            same('clip-'+str(mode),f'BT /F 30 Tf {mode} Tr 20 30 Td (A) Tj ET 1 0 0 rg 0 0 120 90 re f',('20 30 12 18 re S ' if mode in (5,6) else '')+'20 30 12 18 re W n 1 0 0 rg 0 0 120 90 re f',reference=mode==7,secondary=True)
        same('invisible','BT /F 30 Tf 3 Tr 20 30 Td (A) Tj ET','q Q')
        # ISO 32000-1/2 9.3.6 exempts outline-less glyphs; the comparison renderer differs.
        same('empty-clip','BT /F 30 Tf 7 Tr 20 30 Td ( ) Tj ET 1 0 0 rg 0 0 120 90 re f','1 0 0 rg 0 0 120 90 re f',reference=False)
        same('rotated','BT /F 30 Tf 0 1 -1 0 80 20 Tm (A) Tj ET','62 20 18 12 re f')
        same('matrix',text,'2 30 18 12 re f',matrix='[0 .001 -.001 0 0 0]',reference=False)
        same('matrix-shear',text,'20 30 m 32 30 l 41 48 l 29 48 l h f',matrix='[.001 0 .0005 .001 0 0]')
        same('matrix-offset',text,'23 33 12 18 re f',matrix='[.001 0 0 .001 .1 .1]')
        opened=WIDTH+command([0,0],21)+command([400],6)+command([600],7)+b'\x0e'
        same('open-stroke','1 w BT /F 30 Tf 1 Tr 20 30 Td (A) Tj ET','1 w 20 30 m 32 30 l 32 48 l S',glyphs=[('.notdef',WIDTH+b'\x0e'),('A',opened)],reference=False)
        side=command([25,600],13)+BOX[len(WIDTH):]
        accent=command([100,200],13)+command([0,0],21)+command([100],6)+command([100],7)+command([-100],6)+b'\x09\x0e'
        composite=command([25,600],13)+command([100,125,600,65,194],(12,6))
        same('seac','BT /F 30 Tf 20 30 Td <C1> Tj ET','20.75 30 12 18 re f 24.5 48 3 3 re f',metrics='/FirstChar 193 /LastChar 193 /Widths [600]',glyphs=[('.notdef',WIDTH+b'\x0e'),('A',side),('acute',accent),('Aacute',composite)])
        same('mac-expert','BT /F 30 Tf 20 30 Td <47> Tj ET',box,encoding='/MacExpertEncoding',metrics='/FirstChar 71 /LastChar 71 /Widths [600]',glyphs=[('.notdef',WIDTH+b'\x0e'),('onequarter',BOX)])
        # The cubic arch uses the same outline geometry as the independent CFF test.
        from test_pdf_cff_render import fixture as cff_fixture
        from test_pdf_cff import fixture as cff_program
        from test_pdf_cff_render import BOX as cbox
        from test_pdf_type2 import command as t2command
        arch=t2command([0,0],21)+t2command([400,0],5)+t2command([0,400,-400,0,0,-400],8)+b'\x0e'
        a=render('BT /F 30 Tf 20 30 Td (B) Tj ET')
        path.write_bytes(cff_fixture('BT /F 30 Tf 20 30 Td (B) Tj ET',encoding='<< /Differences [66 /customShape] >>',font=cff_program(programs=[b'\x0e',b'\x0e',cbox,arch]),metrics='/FirstChar 66 /LastChar 66 /Widths [600]'))
        b=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
        assert a.returncode==b.returncode==0 and a.stdout==b.stdout,'Type 1 and CFF cubic outlines differ'
        checks+=1
    print(f'Type 1 rendering: {checks} checks passed; {refs} independent comparisons')


if __name__=='__main__':main()
