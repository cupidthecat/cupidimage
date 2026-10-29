#!/usr/bin/env python3
"""Check implicit knockout grouping for combined path and glyph painting."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_cff_render import fixture as cff_pdf
from test_pdf_embedded_fonts import fixture as tt_pdf
from test_pdf_type1_render import fixture as type1_pdf
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm

SETUP='/E gs 1 0 0 rg 0 0 1 RG 4 w '
RECT='20 30 12 18 re '
BACKGROUND='.2 .7 .4 rg 0 0 120 90 re f '


def fixture(kind='cff',form=False,path=False,ais=True,fill=.5,stroke=.5,blend='Normal',tk=True,
            pattern=False,mask=None,mode=2,extra_content='',group=None,pattern_alpha=1,pattern_shape=False):
    state=f'<< /ca {fill} /CA {stroke} /AIS {str(ais).lower()} /BM /{blend} /TK {str(tk).lower()} >>'
    outer=f'<< /AIS {str(ais).lower()}'
    if mask is not None:outer+=' /SMask << /S /Luminosity /G 14 0 R >>'
    outer+=' >>'
    resources='/Font << /F 5 0 R >> /ExtGState << /E 10 0 R /O 13 0 R >> /XObject << /G 11 0 R '+('/W 15 0 R' if group else '')+' >> /Pattern << /P 12 0 R >>'
    setup=SETUP+('/Pattern CS /P SCN ' if pattern else '')
    text=lambda m:f'BT /F 30 Tf {m} Tr 20 30 Td (A) Tj ET '
    separate=setup+(RECT+'f '+RECT+'S ' if path or kind!='bitmap' else text(0)+text(1))
    direct=setup+(RECT+'B ' if path else text(mode))
    if form:
        direct='/G Do '
        if mode==6 and not path:direct+=setup+text(7)
    if mode==6 and not path:direct+=' .1 .8 .3 rg 0 0 120 90 re f '
    program='/O gs '+direct+extra_content
    objects={3:('<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << '+resources+' >> >>').encode(),
             10:state,13:outer,11:stream(separate.encode(),'/Type /XObject /Subtype /Form /BBox [0 0 120 90] /Group << /S /Transparency /I false /K true >>'),
             12:stream(b'0 0 1 rg 0 0 3 6 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 6 6] /XStep 6 /YStep 6 /Resources << >>'),
             14:stream((str(mask or 0)+' g 0 0 120 90 re f').encode(),'/Type /XObject /Subtype /Form /BBox [0 0 120 90] /Group << /S /Transparency /I true /CS /DeviceGray >>')}
    if pattern_alpha!=1 or pattern_shape:
        objects[12]=stream(b'/PAlpha gs 0 0 1 rg 0 0 3 6 re f',
            '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 6 6] /XStep 6 /YStep 6 /Resources << /ExtGState << /PAlpha << /ca '+str(pattern_alpha)+' /AIS '+str(pattern_shape).lower()+' >> >> >>')
    if kind=='bitmap':objects[5]=b'<< /Type /Font /Subtype /Type1 /BaseFont /UnknownBitmap /FirstChar 65 /LastChar 65 /Widths [600] >>'
    if group:
        objects[15]=stream(program.encode(),'/Type /XObject /Subtype /Form /BBox [0 0 120 90] /Group << /S /Transparency '+group+' >>')
        program='/W Do '
    return {'cff':cff_pdf,'tt':tt_pdf,'type1':type1_pdf,'bitmap':tt_pdf}[kind]('1 0 0 1 -1.125 0 cm '+BACKGROUND+program,extra=objects)


def main():
    probe,allocator=[str(Path(a).resolve()) for a in sys.argv[1:3]]
    checks=references=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-combined-paint-') as tmp:
        pdf=Path(tmp)/'case.pdf'
        def render(data):
            nonlocal checks
            pdf.write_bytes(data);result=subprocess.run([probe,str(pdf),'0'],capture_output=True,timeout=30)
            assert result.returncode==0,result.stderr
            checks+=1
            return result.stdout
        def compare(**kwargs):
            actual=render(fixture(**kwargs));expected=render(fixture(form=True,**kwargs))
            assert actual[:12]==expected[:12]
            maximum=max(abs(a-b) for a,b in zip(actual[12:],expected[12:]))
            assert maximum<=1,(kwargs,maximum)
            return actual
        def fault(data):
            nonlocal allocations
            pdf.write_bytes(data);result=subprocess.run([allocator,str(pdf),'0'],capture_output=True,text=True,timeout=180)
            assert result.returncode==0,(result.stdout,result.stderr)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        for kind in ('cff','tt','type1','bitmap'):
            for ais in (False,True):
                for fill,stroke in ((0,0),(.3,0),(1,.25),(.5,.5),(.75,1)):
                    for blend in ('Normal','Multiply'):
                        for tk in (False,True):
                            compare(kind=kind,ais=ais,fill=fill,stroke=stroke,blend=blend,tk=tk)
            fault(fixture(kind=kind))
        for ais in (False,True):
            for fill,stroke in ((0,0),(.3,0),(1,.25),(.5,.5),(.75,1)):
                for blend in ('Normal','Multiply'):
                    compare(path=True,ais=ais,fill=fill,stroke=stroke,blend=blend)
        for pattern in (False,True):
            for mask in (None,0,.25,.75,1):
                for ais in (False,True):
                    for path in (False,True):
                        compare(pattern=pattern,mask=mask,ais=ais,path=path)
            fault(fixture(pattern=pattern,mask=.25))
        for kind in ('cff','tt','type1','bitmap'):
            for tk in (False,True):compare(kind=kind,mode=6,tk=tk,pattern=True,mask=.5)
        for group in ('/I true /K false','/I false /K true','/I true /K true /CS /DeviceGray'):
            for path in (False,True):
                compare(path=path,group=group,mask=.5,pattern=True)
                fault(fixture(path=path,group=group,mask=.5,pattern=True))
        for pattern_alpha in (0,.25):
            for pattern_shape in (False,True):
                for ais in (False,True):
                    compare(pattern=True,pattern_alpha=pattern_alpha,pattern_shape=pattern_shape,ais=ais,mask=.5)
        fault(fixture(pattern=True,pattern_alpha=.25,pattern_shape=True,mask=.5))
        # Resource and paint state remain usable after the implicit surface is released.
        compare(extra_content=' /E gs 0 1 0 rg 50 30 10 18 re f ')
        # Independent arithmetic checks use interior pixels, away from edge raster rules.
        background=[51/255,179/255,102/255]
        for ais in (False,True):
            for fill,stroke in ((0,0),(.3,0),(1,.25),(.5,.5),(.75,1)):
                for mask in (None,.25,.75,1):
                    actual=render(fixture(ais=ais,fill=fill,stroke=stroke,mask=mask,path=True))
                    shape=stroke if ais else 1
                    fill_color=[fill*(i==0)+(1-fill)*background[i] for i in range(3)]
                    stroke_color=[stroke*(i==2)+(1-stroke)*background[i] for i in range(3)]
                    overlap=[stroke*(i==2)+(1-shape)*fill*(i==0)+(1-stroke-(1-shape)*fill)*background[i] for i in range(3)]
                    for x,y,color in ((28,65,overlap),(34,65,fill_color),(25,65,stroke_color),(0,0,background)):
                        m=1 if mask is None else mask
                        expected=[int(255*(m*color[i]+(1-m)*background[i])+.5+1e-9) for i in range(3)]
                        at=12+(y*160+x)*4
                        assert max(abs(a-b) for a,b in zip(actual[at:at+3],expected))<=1,(ais,fill,stroke,mask,x,y,list(actual[at:at+3]),expected)
        if '--reference' in sys.argv:
            # The comparison engines differ on fractional AIS knockout semantics.
            # Pattern placement also has separate known raster differences.
            # Qualify only the independently matching solid-color configurations here.
            import fitz
            fitz.TOOLS.set_aa_level(0)
            for pattern in (False,):
                for mask in (None,.5):
                    actual=render(fixture(form=True,ais=False,pattern=pattern,mask=mask))
                    with fitz.open(pdf) as doc:
                        pix=doc[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                    for x,y in ((28,65),(34,65),(25,65),(0,0)):
                        pixel=y*160+x
                        assert max(abs(a-b) for a,b in zip(actual[12+pixel*4:15+pixel*4],pix.samples[pixel*3:pixel*3+3]))<=2,(pattern,mask,x,y)
                        references+=1
            for path in (False,True):
                actual=render(fixture(ais=True,path=path));prefix=Path(tmp)/'reference'
                run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(pdf),str(prefix)],capture_output=True,timeout=30)
                assert run.returncode==0,run.stderr
                w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                for x,y in ((28,65),(34,65),(25,65),(0,0)):
                    pixel=y*160+x
                    assert max(abs(a-b) for a,b in zip(actual[12+pixel*4:15+pixel*4],rgb[pixel*3:pixel*3+3]))<=2,(path,x,y)
                    references+=1
    print(f'PDF combined paint: {checks} checks; {references} independent color samples; {allocations} allocation checks')


if __name__=='__main__':main()
