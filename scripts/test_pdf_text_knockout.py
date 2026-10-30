#!/usr/bin/env python3
"""Check text-object grouping, glyph overlap and persistent graphics state."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm
from test_pdf_embedded_fonts import FONT
from test_pdf_forms import form


def fixture(content, state='', program='18 0 d0 0 0 18 12 re f', extra=None, resources=''):
    font='<< /Type /Font /Subtype /Type3 /FontBBox [0 0 18 12] /FontMatrix [1 0 0 1 0 0] /Encoding << /Differences [65 /A] >> /FirstChar 65 /LastChar 65 /Widths [18] /CharProcs << /A 6 0 R >> >>'
    objects={5:font,6:stream(program)};objects.update(extra or {})
    states='/ExtGState << /G << /ca .5 '+state+' >> /Opaque << /ca 1 >> /Off << /TK false >> /Z << /ca 0 >> >>'
    return document('/MediaBox [0 0 18 12] /Resources << /Font << /F 5 0 R >> '+states+' '+resources+' >>',stream=content,extra=objects)


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=refs=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-text-knockout-') as tmp:
        path=Path(tmp)/'case.pdf'
        def check(name,pdf,expected=None,reject=False,reference=True):
            nonlocal checks,refs
            path.write_bytes(pdf);result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert result.returncode==(3 if reject else 0),(name,result.returncode,result.stderr)
            checks+=1
            if reject:return
            assert struct.unpack('=III',result.stdout[:12])==(24,16,1)
            want=bytes(v for y in range(16) for x in range(24) for v in (*expected(x,y),255))
            assert result.stdout[12:]==want,(name,'analytic differs',next((i,a,b) for i,(a,b) in enumerate(zip(result.stdout[12:],want)) if a!=b))
            if '--reference' in sys.argv and reference:
                prefix=Path(tmp)/'reference';run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                assert run.returncode==0,run.stderr;w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                actual=bytes(v for i,v in enumerate(result.stdout[12:]) if i%4!=3)
                assert (w,h)==(24,16) and max(abs(a-b) for a,b in zip(actual,rgb))<=1,(name,'independent differs',max(abs(a-b) for a,b in zip(actual,rgb)))
                refs+=1
        half=lambda x,y:(255,128,128)
        double=lambda x,y:(255,64,64)
        for state in ('','/TK true','/TK 7 0 R','/TK null'):
            for text in ('-18 Tc (AA) Tj','[(A) 18000 (A)] TJ','(A) Tj 1 0 0 1 0 0 Tm (A) Tj'):
                check('knockout-'+state+text,fixture('/G gs 1 0 0 rg BT /F 1 Tf '+text+' ET',state,extra={7:b'true'}),half,reference=False)
        for text in ('-18 Tc (AA) Tj','[(A) 18000 (A)] TJ','(A) Tj 1 0 0 1 0 0 Tm (A) Tj'):
            check('separate-glyphs-'+text,fixture('/G gs 1 0 0 rg BT /F 1 Tf '+text+' ET','/TK false'),double)
        text='/G gs BT /F 1 Tf 1 0 0 rg (A) Tj 1 0 0 1 0 0 Tm 0 0 1 rg (A) Tj ET'
        check('later-color-wins',fixture(text),lambda x,y:(128,128,255),reference=False)
        check('later-color-blends',fixture(text,'/TK false'),lambda x,y:(128,64,191))
        check('separate-text-objects',fixture('/G gs 1 0 0 rg BT /F 1 Tf (A) Tj ET BT /F 1 Tf (A) Tj ET'),double)
        check('empty-text',fixture('/G gs BT /F 1 Tf ET 1 0 0 rg 0 0 18 12 re f'),half)
        check('state-persists',fixture('BT /G gs /F 1 Tf ET 1 0 0 rg 0 0 18 12 re f'),half)
        check('state-not-double-applied',fixture('BT /G gs /F 1 Tf 1 0 0 rg (A) Tj ET'),half)
        check('q-Q-selects-whole-object',fixture('q /Off gs Q /G gs 1 0 0 rg BT /F 1 Tf -18 Tc (AA) Tj ET'),half,reference=False)
        check('uncolored-glyph-overlap',fixture('/G gs 1 0 0 rg BT /F 1 Tf -18 Tc (AA) Tj ET',program='18 0 0 0 18 12 d1 0 0 18 12 re f'),half,reference=False)
        check('internal-glyph-paints-compose',fixture('/G gs 1 0 0 rg BT /F 1 Tf (A) Tj ET',program='18 0 d0 0 0 18 12 re f 0 0 18 12 re f'),double)
        check('uncolored-internal-overlap',fixture('/G gs 1 0 0 rg BT /F 1 Tf (A) Tj ET',program='18 0 0 0 18 12 d1 0 0 18 12 re f 0 0 18 12 re f'),half)
        check('late-invisible-glyph-does-not-erase',fixture('/G gs 1 0 0 rg BT /F 1 Tf (A) Tj 3 Tr 1 0 0 1 0 0 Tm (A) Tj ET'),half)
        mask=form('/H gs 0 0 18 12 re f','/BBox [0 0 18 12] /Group << /S /Transparency /I true >> /Resources << /ExtGState << /H << /ca .5 >> >> >>')
        check('uncolored-mask-once',fixture('/G gs 1 0 0 rg BT /F 1 Tf (A) Tj ET',state='/SMask << /S /Alpha /G 10 0 R >>',program='18 0 0 0 18 12 d1 0 0 18 12 re f 0 0 18 12 re f',extra={10:mask}),lambda x,y:(255,191,191))
        check('missing-glyph-does-not-knock-out',fixture('/G gs 1 0 0 rg BT /F 1 Tf (A) Tj 1 0 0 1 0 0 Tm (B) Tj ET'),half)
        font_objects={5:b'<< /Type /Font /Subtype /TrueType /BaseFont /CupidImageTestShapes /FontDescriptor 8 0 R /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 65 /Widths [1000] >>',
                      8:b'<< /Type /FontDescriptor /FontName /CupidImageTestShapes /Flags 32 /FontBBox [0 0 1000 1400] /ItalicAngle 0 /Ascent 1000 /Descent -200 /CapHeight 600 /StemV 80 /FontFile2 9 0 R >>',9:stream(FONT)}
        for tk in (False,True):
            check('truetype-'+str(tk),fixture('/G gs 1 0 0 rg BT /F 20 Tf 225 Tz -20 Tc (AA) Tj ET','/TK '+str(tk).lower(),extra=font_objects),half if tk else double,reference=not tk)
        check('zero-opacity-later-glyph',fixture('0 1 0 rg 0 0 18 12 re f /G gs 1 0 0 rg BT /F 1 Tf (A) Tj 1 0 0 1 0 0 Tm /Z gs (A) Tj ET'),lambda x,y:(0,255,0),reference=False)
        for state in ('/TK 1','/TK /true','/TK []'):
            check('invalid-'+state,fixture('/G gs BT /F 1 Tf (A) Tj ET',state),reject=True)
        check('mid-object-change',fixture('BT /Off gs /F 1 Tf (A) Tj ET'),reject=True)
        check('unterminated',fixture('BT /F 1 Tf (A) Tj'),reject=True)
    print(f'PDF text knockout: {checks} checks passed; {refs} independent comparisons')


if __name__=='__main__':main()
