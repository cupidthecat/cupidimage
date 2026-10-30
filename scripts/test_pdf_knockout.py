#!/usr/bin/env python3
"""Check separate shape/opacity, knockout backdrops and cached pattern shape."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_groups import fixture
from test_pdf_forms import form
from test_pdf_type3 import stream
from test_pdf_transparency import MODES, blend
from test_pdf_pages import document


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=refs=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-knockout-') as tmp:
        path=Path(tmp)/'case.pdf'
        def check(name,pdf,expected=None,reference=True,reject=False):
            nonlocal checks,refs
            path.write_bytes(pdf);result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert result.returncode==(3 if reject else 0),(name,result.returncode,result.stderr)
            checks+=1
            if reject:return
            assert struct.unpack('=III',result.stdout[:12])==(24,16,1)
            want=bytes(v for y in range(16) for x in range(24) for v in (*expected(x,y),255))
            assert result.stdout[12:]==want,(name,'analytic differs',next((i,a,b) for i,(a,b) in enumerate(zip(result.stdout[12:],want)) if a!=b))
            if '--reference' in sys.argv and reference:
                import fitz
                fitz.TOOLS.set_aa_level(0)
                with fitz.open(path) as document:
                    pixmap=document[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                    w,h,rgb=pixmap.width,pixmap.height,pixmap.samples
                actual=bytes(v for i,v in enumerate(result.stdout[12:]) if i%4!=3)
                assert (w,h)==(24,16) and max(abs(a-b) for a,b in zip(actual,rgb))<=1,(name,'independent differs',max(abs(a-b) for a,b in zip(actual,rgb)))
                refs+=1
        rect='0 0 18 12 re f'
        red='1 0 0 rg '+rect+' '
        blue='0 0 1 rg '+rect
        for isolated in (True,False):
            group='<< /S /Transparency /I '+str(isolated).lower()+' /K true >>'
            content='0 1 0 rg '+rect+' /G Do'
            for ais in (False,True):
                for opacity in (0,.2,.4,.5,.8,1):
                    attrs='/Resources << /ExtGState << /B << /ca '+str(opacity)+' /AIS '+str(ais).lower()+' >> >> >>'
                    if ais: expected=(int(255*(1-opacity)+.5),0,int(255*opacity+.5))
                    else: expected=(0,int(255*(1-opacity)+.5),int(255*opacity+.5))
                    check(f'constant-{isolated}-{ais}-{opacity}',fixture(program=red+'/B gs '+blue,group=group,attrs=attrs,content=content),lambda x,y:expected,reference=(opacity!=0 or ais) and opacity!=.5 and (not ais or opacity in (0,1)))
        group='<< /S /Transparency /I true /K true >>'
        check('shape-scope',fixture(program=red+'q /B gs Q /H gs '+blue,group=group,attrs='/Resources << /ExtGState << /B << /AIS true >> /H << /ca .8 >> >> >>'),lambda x,y:(51,51,255))
        attrs='/Resources << /ExtGState << /B << /AIS 7 0 R /ca .8 >> /Null << /AIS null >> >> >>'
        for clear in ('','/Null gs '):
            check('indirect-shape-'+clear,fixture(program=red+'/B gs '+clear+blue,group=group,attrs=attrs,extra={7:b'true'}),lambda x,y:(51,0,204),reference=False)
        for invalid in ('1','/True','[]'):
            check('invalid-AIS-'+invalid,fixture(program='/B gs '+blue,group=group,attrs='/Resources << /ExtGState << /B << /AIS '+invalid+' >> >> >>'),reject=True)
        for ais in (False,True):
            mask=form('.5 g '+rect,'/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS /DeviceGray >>')
            attrs='/Resources << /ExtGState << /B << /AIS '+str(ais).lower()+' /SMask << /S /Luminosity /G 6 0 R >> >> >> >>'
            # Preserve the exact .5 mask value until the final output quantization.
            expected=(128,0,128) if ais else (128,128,255)
            check('soft-mask-'+str(ais),fixture(program=red+'/B gs '+blue,group=group,attrs=attrs,extra={6:mask}),lambda x,y:expected,reference=False)
        for soft in (False,True):
            mask=stream(bytes([0]),'/Subtype /Image /Width 1 /Height 1 '+('/BitsPerComponent 8 /ColorSpace /DeviceGray' if soft else '/ImageMask true /Decode [1 0]'))
            image=stream(bytes([0,0,255]),'/Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB /'+('SMask' if soft else 'Mask')+' 7 0 R')
            attrs='/Resources << /XObject << /Im 6 0 R >> >>'
            check('zero-image-mask-'+str(soft),fixture(program=red+'18 0 0 12 0 0 cm /Im Do',group=group,attrs=attrs,extra={6:image,7:mask}),lambda x,y:(255,255,255) if soft else (255,0,0),reference=not soft)
        nested=form('/Screen gs 0 0 1 rg '+rect,'/BBox [0 0 18 12] /Group << /S /Transparency /I false >>')
        check('nested-initial-backdrop',fixture(program=red+'/N Do',group='<< /S /Transparency /K true >>',content='0 1 0 rg '+rect+' /G Do',attrs='/Resources << /XObject << /N 6 0 R >> /ExtGState << /Screen << /BM /Screen >> >> >>',extra={6:nested}),lambda x,y:(0,255,255))
        for opacity in (0,.8):
            pattern=stream('/B gs 0 0 1 rg 0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 3 3] /XStep 6 /YStep 6 /Resources << /ExtGState << /B << /ca '+str(opacity)+' >> >> >>')
            expected=(int(255*(1-opacity)+.5),int(255*(1-opacity)+.5),255)
            check('pattern-shape-'+str(opacity),fixture(program=red+'/Pattern cs /P scn '+rect,group=group,resources='/Pattern << /P 6 0 R >>',extra={6:pattern}),lambda x,y:expected if x%8<4 and y%8>=4 else (255,0,0),reference=opacity!=0)
        for mode in MODES:
            for isolated in (True,False):
                source=(0.,0.,1.); backdrop=(0.,1.,0.)
                mixed=source if isolated else blend(mode,source,backdrop)
                expected=tuple(int((.8*c+.2*b)*255+.5) for c,b in zip(mixed,backdrop))
                attrs='/Resources << /ExtGState << /B << /ca .8 /BM /'+mode+' >> >> >>'
                g='<< /S /Transparency /K true /I '+str(isolated).lower()+' >>'
                check('blend-'+mode+str(isolated),fixture(program=red+'/B gs '+blue,group=g,attrs=attrs,content='0 1 0 rg '+rect+' /G Do'),lambda x,y:expected,reference=False)
        invisible=form('/Z gs 0 0 1 rg '+rect,'/BBox [0 0 18 12] /Group << /S /Transparency /I true >> /Resources << /ExtGState << /Z << /ca 0 /AIS false >> >> >>')
        for ais in (False,True):
            attrs='/Resources << /XObject << /N 6 0 R >> /ExtGState << /B << /ca .5 /AIS '+str(ais).lower()+' >> >> >>'
            check('transparent-nested-shape-'+str(ais),fixture(program=red+'/B gs /N Do',group=group,attrs=attrs,extra={6:invisible}),lambda x,y:(255,128,128) if ais else (255,255,255),reference=False)
        page=document('/MediaBox [0 0 18 12] /Group << /S /Transparency /K true >> /Resources << /ExtGState << /B << /ca .8 >> >> >>',stream=red+'/B gs '+blue)
        check('knockout-page',page,lambda x,y:(51,51,255),reference=False)
        check('transparent-stroke-knocks-out',fixture(program=red+'/ZeroStroke gs 0 0 1 rg 0 0 1 RG 2.8 w 3 3 12 6 re B',group=group),lambda x,y:(255,255,255) if 2<=x<22 and 2<=y<14 and not(6<=x<18 and 6<=y<10) else (0,0,255) if 6<=x<18 and 6<=y<10 else (255,0,0),reference=False)
    print(f'PDF knockout: {checks} checks passed; {refs} independent comparisons')


if __name__=='__main__':main()
