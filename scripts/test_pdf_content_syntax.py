#!/usr/bin/env python3
"""Render escaped resource names and validate text-showing operands."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_text_knockout import fixture
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm


def newline_font():
    widths=' '.join(['18']*66)
    return '<< /Type /Font /Subtype /Type3 /FontBBox [0 0 18 12] /FontMatrix [1 0 0 1 0 0] /Encoding << /Differences [0 /A 10 /A 13 /B 65 /A] >> /FirstChar 0 /LastChar 65 /Widths ['+widths+'] /CharProcs << /A 6 0 R /B 7 0 R >> >>'


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=refs=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-content-syntax-') as tmp:
        path=Path(tmp)/'case.pdf'
        def check(name,pdf,color=(255,0,0),reject=False,reference=True,secondary=False):
            nonlocal checks,refs
            path.write_bytes(pdf);result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert result.returncode==(3 if reject else 0),(name,result.returncode,result.stderr)
            checks+=1
            if reject:return
            assert struct.unpack('=III',result.stdout[:12])==(24,16,1)
            assert result.stdout[12:]==bytes((*color,255))*(24*16),(name,'pixels differ')
            if '--reference' in sys.argv and reference:
                if secondary:
                    import fitz
                    fitz.TOOLS.set_aa_level(0)
                    with fitz.open(path) as ref:
                        pix=ref[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                        w,h,rgb=pix.width,pix.height,pix.samples
                else:
                    prefix=Path(tmp)/'reference';run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                    assert run.returncode==0,run.stderr
                    w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                assert (w,h)==(24,16) and rgb==bytes(color)*(24*16),(name,'independent differs')
                refs+=1
        for name in ('F','#46'):
            for key in ('F','#46'):
                page='<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 18 12] /Resources << /Font << /'+key+' 5 0 R >> >> >>'
                check('font-name-'+name+'-'+key,fixture('1 0 0 rg BT /'+name+' 1 Tf (A) Tj ET',extra={3:page}))
        for name in ('F#2F','F#20A','#28#29','F#25'):
            page='<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 18 12] /Resources << /Font << /'+name+' 5 0 R >> >> >>'
            check('font-delimiter-'+name,fixture('1 0 0 rg BT /'+name+' 1 Tf (A) Tj ET',extra={3:page}))
        page='<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 18 12] /Resources << /Font << /F#2323 5 0 R >> /ExtGState << /G#231 << /ca 1 >> >> >> >>'
        check('decode-name-once',fixture('/G#231 gs 1 0 0 rg BT /F#2323 1 Tf (A) Tj ET',extra={3:page}))
        check('font-state-outside-text',fixture('/F 1 Tf 1 0 0 rg BT (A) Tj ET'))
        for text in ('[(A)]','[<41>]','[0 (A) 0]','[% comment with ]\n(A)]','[(\\101)]','[<4 1>]'):
            check('TJ-'+text,fixture('1 0 0 rg BT /F 1 Tf '+text+' TJ ET'))
        font=newline_font()
        for text,color in (('(\r)',(255,0,0)),('(\r\n)',(255,0,0)),('(\n)',(255,0,0)),('(\\n)',(255,0,0)),('(\\r)',(255,255,255)),('(\\\r\n)',(255,255,255)),('<0A>',(255,0,0)),('<0D>',(255,255,255)),('<0>',(255,0,0)),('<\0 0 A>',(255,0,0))):
            for array in (False,True):
                content='1 0 0 rg BT /F 1 Tf '+('['+text+'] TJ' if array else text+' Tj')+' ET'
                check('string-'+repr(text)+str(array),fixture(content,extra={5:font,7:stream('18 0 d0')}),color,secondary=text in ('(\r)','(\r\n)'))
        for token in ('[true]','[null]','[/A]','[[(A)]]','[<< /A 1 >>]','[1e3]','[1..2]','[<41]','[(A)] 0','0','(A)'):
            check('invalid-TJ-'+token,fixture('BT /F 1 Tf '+token+' TJ ET'),reject=True)
        for content in ('(A) Tj','[(A)] TJ','(A) \'','0 0 (A) "','ET','1 BT ET','BT 1 ET','BT /F 1 Tf 1 (A) Tj ET','BT /F 1 Tf 1 Tj ET','0 0 Td','0 0 TD','1 0 0 1 0 0 Tm','T*','BT 1 T* ET','<41','BT /F 1 Tf <41','/#00 1 Tf','/#G0 1 Tf','/'+'#','9'*64+' g','1e3 g',' '.join(['0']*65)+' g'):
            check('invalid-content-'+content,fixture(content),reject=True)
    print(f'PDF content rendering: {checks} checks passed; {refs} independent comparisons')


if __name__=='__main__':main()
