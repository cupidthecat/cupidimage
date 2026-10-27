#!/usr/bin/env python3
"""Verify isolated RGB group compositing without treating ordinary forms as groups."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_type3 import stream
from test_pdf_forms import form
from audit_pdf_corpus import read_ppm

ALPHA='0.5019607843137255'
STATES=f'/ExtGState << /A << /ca {ALPHA} /CA {ALPHA} >> /Screen << /BM /Screen >> /ZeroStroke << /CA 0 /ca 1 >> >>'


def fixture(program='1 0 0 rg 0 0 18 12 re f',content='/G Do',group='<< /S /Transparency /I true /CS /DeviceRGB >>',attrs='',extra=None,resources='',bbox='0 0 18 12'):
    objects={5:form(program,'/BBox ['+bbox+'] '+('/Group '+group if group else '')+' '+attrs)}
    objects.update(extra or {})
    return document('/MediaBox [0 0 18 12] /Resources << /XObject << /G 5 0 R >> '+STATES+' '+resources+' >>',stream=content,extra=objects)


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=refs=0
    white=(255,255,255);red=(255,0,0);blue=(0,0,255)
    with tempfile.TemporaryDirectory(prefix='cupidimage-groups-') as tmp:
        path=Path(tmp)/'case.pdf'
        def check(name,pdf,expected=None,reject=False,tolerance=0,secondary=False,reference=True):
            nonlocal checks,refs
            path.write_bytes(pdf);result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert result.returncode==(3 if reject else 0),(name,result.returncode,result.stderr)
            checks+=1
            if reject:return
            assert struct.unpack('=III',result.stdout[:12])==(24,16,1)
            if expected:
                wanted=bytes(v for y in range(16) for x in range(24) for v in (*expected(x,y),255))
                assert result.stdout[12:]==wanted,(name,'analytic pixels differ',next((i,a,b) for i,(a,b) in enumerate(zip(result.stdout[12:],wanted)) if a!=b))
            if '--reference' in sys.argv and reference:
                if secondary:
                    import fitz
                    fitz.TOOLS.set_aa_level(0)
                    with fitz.open(path) as ref:
                        pix=ref[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                        w,h,rgb=pix.width,pix.height,pix.samples
                else:
                    prefix=Path(tmp)/'reference';r=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                    assert r.returncode==0,r.stderr;w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                actual=bytes(v for i,v in enumerate(result.stdout[12:]) if i%4!=3)
                assert (w,h)==(24,16) and max(abs(a-b) for a,b in zip(actual,rgb))<=tolerance,(name,'independent pixels differ',max(abs(a-b) for a,b in zip(actual,rgb)))
                refs+=1
        check('opaque',fixture(),lambda x,y:red)
        check('empty',fixture(''),lambda x,y:white)
        check('group-alpha',fixture(content='/A gs /G Do'),lambda x,y:(255,127,127))
        overlap='1 0 0 rg 0 0 11.8 12 re f 0 0 1 rg 6 0 12 12 re f'
        check('alpha-applied-once',fixture(overlap,'/A gs /G Do'),lambda x,y:(255,127,127) if x<8 else (127,127,255))
        check('member-alpha',fixture('/A gs '+overlap),lambda x,y:(255,127,127) if x<8 else (127,63,191) if x<16 else (127,127,255),tolerance=1)
        check('outer-blend-reset-inside',fixture(overlap,'0 1 0 rg 0 0 18 12 re f /Screen gs /G Do'),lambda x,y:(255,255,0) if x<8 else (0,255,255))
        check('stroke-alpha-reset',fixture('0 0 1 RG 2.8 w 0 3 m 18 3 l S','/ZeroStroke gs /G Do'),lambda x,y:blue if 10<=y<14 else white)
        check('bbox',fixture(bbox='0 0 8.8 12'),lambda x,y:red if x<12 else white,reference=False)
        check('parent-clip',fixture(content='0 0 8.8 12 re W n /G Do'),lambda x,y:red if x<12 else white)
        check('inner-clip',fixture('0 0 8.8 12 re W n 1 0 0 rg 0 0 18 12 re f'),lambda x,y:red if x<12 else white)
        check('matrix',fixture(attrs='/Matrix [.49 0 0 .49 0 0]'),lambda x,y:red if x<12 and y>=8 else white)
        check('repeated-group',fixture(content='/A gs /G Do /G Do'),lambda x,y:(255,63,63),tolerance=1)
        inner=form('1 0 0 rg 0 0 18 12 re f','/BBox [0 0 18 12] /Group << /S /Transparency /I true >>')
        check('nested-alpha',fixture('/A gs /Inner Do','/A gs /G Do',attrs='/Resources << /XObject << /Inner 6 0 R >> '+STATES+' >>',extra={6:inner}),lambda x,y:(255,191,191),tolerance=1)
        check('inherited-colour-space',fixture(group='<< /S /Transparency /I true >>'),lambda x,y:red)
        check('indirect-attributes',fixture(group='6 0 R',extra={6:b'<< /S 7 0 R /I 8 0 R /K 9 0 R /CS 10 0 R >>',7:b'/Transparency',8:b'true',9:b'false',10:b'/DeviceRGB'}),lambda x,y:red)
        check('named-colour-space',fixture(group='<< /S /Transparency /I true /CS /Rgb >>',attrs='/Resources << /ColorSpace << /Rgb /DeviceRGB >> >>'),lambda x,y:red)
        check('null-optional-fields',fixture(group='<< /S /Transparency /I true /K null /CS null >>'),lambda x,y:red)
        # Null Group is an ordinary form; its two overlapping members composite independently.
        check('null-group',fixture(overlap,'/A gs /G Do',group='null'),lambda x,y:(255,127,127) if x<8 else (127,63,191) if x<16 else (127,127,255),tolerance=1)
        type3=b'<< /Type /Font /Subtype /Type3 /FontBBox [0 0 6 6] /FontMatrix [1 0 0 1 0 0] /Encoding << /Differences [65 /A] >> /FirstChar 65 /LastChar 65 /Widths [6] /CharProcs << /A 8 0 R >> >>'
        check('text-in-group',fixture('BT /F 1 Tf (AA) Tj ET','/A gs /G Do',attrs='/Resources << /Font << /F 7 0 R >> >>',extra={7:type3,8:stream('6 0 d0 0 0 5.8 5.8 re f')}),lambda x,y:(127,127,127) if x<16 and y>=8 else white)
        image=stream(bytes([255,0,0]),'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB')
        check('image-in-group',fixture('18 0 0 12 0 0 cm /Im Do','/A gs /G Do',attrs='/Resources << /XObject << /Im 6 0 R >> >>',extra={6:image}),lambda x,y:(255,127,127))
        shading=b'<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Extend [true true] /Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [1 0 0] /N 1 >> >>'
        check('shading-in-group',fixture('/Sh sh','/A gs /G Do',attrs='/Resources << /Shading << /Sh 6 0 R >> >>',extra={6:shading}),lambda x,y:(255,127,127))
        pattern=stream('1 0 0 rg 0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 3 3] /XStep 6 /YStep 6 /Resources << >>')
        check('pattern-in-group',fixture('/Pattern cs /P scn 0 0 18 12 re f','/A gs /G Do',attrs='/Resources << /Pattern << /P 6 0 R >> >>',extra={6:pattern}),lambda x,y:(255,127,127) if x%8<4 and y%8>=4 else white)
        check('parent-state-preserved',fixture(content='0 1 0 rg /G Do 0 0 2.8 2.8 re f'),lambda x,y:(0,255,0) if x<4 and y>=12 else red)
        check('cyclic-group-flag',fixture(group='<< /S /Transparency /I 6 0 R >>',extra={6:b'6 0 R'}),reject=True)
        for group in ('<< /S /Transparency >>','<< /S /Transparency /I false >>','<< /S /Transparency /I true /K true >>','<< /S /Other /I true >>','<< /I true >>','<< /S /Transparency /I 1 >>','<< /S /Transparency /I truex >>','<< /S /Transparency /I true /CS /DeviceCMYK >>','<< /S /Transparency /I true /CS /DeviceGray >>','<< /S /Transparency /I true /CS /Pattern >>','[]','12'):
            check('unsupported-or-invalid',fixture(group=group),reject=True)
        check('default-colour-space',fixture(attrs='/Resources << /ColorSpace << /DefaultRGB [/CalRGB << /WhitePoint [1 1 1] >>] >> >>'),reject=True)
        check('group-cycle',fixture('/G Do'),reject=True)
    print(f'Isolated RGB groups: {checks} checks passed; {refs} independent comparisons')

if __name__=='__main__':main()
