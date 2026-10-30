#!/usr/bin/env python3
"""Check outline-less text clipping and deferred text-surface allocation."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_cff_render import fixture as cff_pdf
from test_pdf_embedded_fonts import fixture as tt_pdf, FONT
from test_pdf_truetype import replace_tables
from test_pdf_truetype_selection import cmap
from test_pdf_type1_render import fixture as type1_pdf
from test_pdf_type3 import stream

FILL='1 0 0 rg 0 0 120 90 re f'
EMPTY_SPACE_FONT=replace_tables(FONT,{b'cmap':cmap([(3,1,65,1)])})


def fixture(content,kind='cff',encoding='/WinAnsiEncoding',extra=None,large=False):
    objects=dict(extra or {})
    if kind in ('bitmap','standard','substitute'):
        name={'bitmap':'UnknownBitmap','standard':'Helvetica','substitute':'Arial'}[kind]
        objects[5]=('<< /Type /Font /Subtype /Type1 /BaseFont /'+name+' /Encoding '+encoding+' /FirstChar 32 /LastChar 65 /Widths ['+'600 '*34+'] >>').encode()
    if large:
        objects[3]=b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 1500 1500] /Resources << /Font << /F 5 0 R >> >> >>'
    maker={'cff':cff_pdf,'tt':tt_pdf,'type1':type1_pdf,'bitmap':tt_pdf,'standard':tt_pdf,'substitute':tt_pdf}[kind]
    if kind=='tt':
        return maker(content,encoding=encoding,extra=objects,font_data=EMPTY_SPACE_FONT)
    return maker(content,encoding=encoding,extra=objects)


def main():
    probe,allocator=[str(Path(a).resolve()) for a in sys.argv[1:3]]
    checks=references=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-empty-glyphs-') as tmp:
        pdf=Path(tmp)/'case.pdf'
        def render(data):
            nonlocal checks
            pdf.write_bytes(data);result=subprocess.run([probe,str(pdf),'0'],capture_output=True,timeout=45)
            assert result.returncode==0,result.stderr
            checks+=1
            return result.stdout
        def same(a,b,**kwargs):
            assert render(fixture(a,**kwargs))==render(fixture(b,**kwargs)),(kwargs,a,b)
        def fault(data):
            nonlocal allocations
            pdf.write_bytes(data);result=subprocess.run([allocator,str(pdf),'0'],capture_output=True,text=True,timeout=120)
            assert result.returncode==0,(result.stdout,result.stderr)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        for kind in ('cff','tt','type1','bitmap','standard','substitute'):
            for mode in range(4,8):
                for show in ('() Tj','( ) Tj','(   ) Tj','[() -100 ( ) 200 ( )] TJ'):
                    prefix=f'BT /F 30 Tf {mode} Tr 20 30 Td '
                    same(prefix+show+' ET '+FILL,FILL,kind=kind)
                for clip in ('0 0 60 90 re W n ','0 0 0 0 re W n '):
                    same(clip+f'BT /F 30 Tf {mode} Tr ( ) Tj ET '+FILL,clip+FILL,kind=kind)
                for zero in ('/F 0 Tf','/F 30 Tf 0 Tz'):
                    same(f'BT {zero} {mode} Tr ( ) Tj ET '+FILL,FILL,kind=kind)
                    same(f'BT {zero} {mode} Tr (A) Tj ET '+FILL,'q Q',kind=kind)
            same('BT /F 30 Tf 7 Tr ( ) Tj 1 0 0 1 20 30 Tm (A) Tj ( ) Tj ET '+FILL,
                 'BT /F 30 Tf 7 Tr 20 30 Td (A) Tj ET '+FILL,kind=kind)
            same('BT /F 30 Tf 7 Tr 10000 10000 Td (A) Tj ET '+FILL,'q Q',kind=kind)
            same('q BT /F 30 Tf 7 Tr (A) Tj ET Q BT /F 30 Tf 7 Tr ( ) Tj ET '+FILL,FILL,kind=kind)
            fault(fixture('BT /F 30 Tf 7 Tr ( ) Tj 1 0 0 1 20 30 Tm (A) Tj ET '+FILL,kind=kind))
        for kind in ('cff','tt','type1','standard','substitute'):
            # Decisions follow actual selected outlines, never character code 32.
            same('BT /F 30 Tf 7 Tr (A) Tj ET '+FILL,FILL,kind=kind,encoding='<< /Differences [65 /space] >>')
            actual=render(fixture('BT /F 30 Tf 7 Tr 20 30 Td ( ) Tj ET '+FILL,kind=kind,encoding='<< /Differences [32 /A] >>'))
            expected=render(fixture('1 0 0 rg BT /F 30 Tf 20 30 Td (A) Tj ET',kind=kind))
            assert actual==expected,kind
        # The original authored TrueType fixture deliberately maps code 32 to ink.
        actual=render(tt_pdf('BT /F 30 Tf 7 Tr 20 30 Td ( ) Tj ET '+FILL))
        expected=render(tt_pdf('1 0 0 rg BT /F 30 Tf 20 30 Td (A) Tj ET'))
        assert actual==expected
        # The substitute's .notdef has ink; glyph zero is not automatically empty.
        encoding='<< /Differences [65 /UnmappedName] >>'
        actual=render(fixture('BT /F 30 Tf 7 Tr 20 30 Td (A) Tj ET '+FILL,kind='substitute',encoding=encoding))
        expected=render(fixture('1 0 0 rg BT /F 30 Tf 20 30 Td (A) Tj ET',kind='substitute',encoding=encoding))
        assert actual==expected and any(v!=255 for v in actual[12:])
        # Empty text on a page too large for an implicit group must still render.
        marker='1 0 0 rg 20 30 3 3 re f'
        baseline=hashlib.sha256(render(fixture(marker,kind='tt',large=True))).digest()
        for mode in (0,7):
            actual=render(fixture(f'BT /F 30 Tf {mode} Tr (   ) Tj ET '+marker,kind='tt',large=True))
            assert hashlib.sha256(actual).digest()==baseline
        for mode in (0,7):
            same((f'BT /F 30 Tf {mode} Tr ( ) Tj ET '*5000)+FILL,FILL,kind='cff')
        # Deferred groups still collect overlapping visible glyphs across shows.
        same('/Half gs BT /F 30 Tf 20 30 Td ( ) Tj 1 0 0 1 20 30 Tm (A) Tj 1 0 0 1 20 30 Tm (A) Tj ET',
             '/Half gs BT /F 30 Tf 20 30 Td (A) Tj ET',kind='tt')
        if '--reference' in sys.argv:
            # Outline-less clipping differs in the comparison renderer. Only
            # nonempty selection/spacing cases are qualified independently.
            for kind in ('cff','tt','type1'):
                data=fixture('BT /F 30 Tf 7 Tr ( ) Tj 1 0 0 1 20 30 Tm (A) Tj ET '+FILL,kind=kind)
                actual=render(data)
                import fitz
                fitz.TOOLS.set_aa_level(0)
                with fitz.open(pdf) as doc:
                    raster=doc[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                    w,h,rgb=raster.width,raster.height,raster.samples
                native=bytes(v for i,v in enumerate(actual[12:]) if i%4!=3)
                assert (w,h)==(160,120) and native==rgb,kind
                references+=1
    print(f'PDF empty glyphs: {checks} checks; {references} independent comparisons; {allocations} allocation checks')


if __name__=='__main__':main()
