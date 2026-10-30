#!/usr/bin/env python3
"""Analytic axial/radial shading tests and native-resolution oracle checks."""
from pathlib import Path
import math
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from audit_pdf_corpus import read_ppm


def shading_pdf(shading, function, content='/Sh sh', size=(18,12), extra=None, inherited=False):
    resources='/Resources << /Shading << /Sh 5 0 R >> /ExtGState << /Half << /ca .5 >> >> >>'
    objects={5:shading,6:function};objects.update(extra or {})
    return document(f'/MediaBox [0 0 {size[0]} {size[1]}] '+('' if inherited else resources),
                    resources if inherited else '', '0.5 g -1000 -1000 3000 3000 re f '+content, objects)


def main():
    probe=str(Path(sys.argv[1]).resolve());count=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-shading-')as directory:
        def check(name,shading,function,expected=None,content='/Sh sh',size=(18,12),extra=None,reject=False,reference=False,inherited=False,reference_limit=1,secondary=False):
            nonlocal count
            count+=1;path=Path(directory)/(name+'.pdf')
            path.write_bytes(shading_pdf(shading,function,content,size,extra,inherited))
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert run.returncode==(3 if reject else 0),(name,run.stderr)
            if reject:return
            w,h,pages=struct.unpack('=III',run.stdout[:12]);assert(w,h,pages)==(round(size[0]*4/3),round(size[1]*4/3),1)
            if expected:
                want=bytes(c for y in range(h)for x in range(w)for c in (*expected(x,y,w,h),255))
                assert run.stdout[12:]==want,(name,next((i,a,b)for i,(a,b)in enumerate(zip(run.stdout[12:],want))if a!=b))
            if '--reference'in sys.argv and reference:
                if secondary:
                    import fitz
                    with fitz.open(path) as reference_document:
                        pixmap=reference_document[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                        rw,rh,rgb=pixmap.width,pixmap.height,pixmap.samples
                else:
                    prefix=Path(directory)/'reference' 
                    subprocess.run(['pdftoppm','-singlefile','-r','96','-aaVector','no',str(path),str(prefix)],capture_output=True,check=True,timeout=15)
                    rw,rh,rgb=read_ppm(prefix.with_suffix('.ppm'))
                assert(rw,rh)==(w,h)
                actual=bytes(c for i,c in enumerate(run.stdout[12:])if i%4!=3)
                error=max(abs(a-b)for a,b in zip(actual,rgb))
                assert error<=reference_limit,(name,'reference differs',error)
        function='<< /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >>'
        axial='<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Function 6 0 R >>'
        rgb=lambda t:(int(255*(1-t)+.5),0,int(255*t+.5))
        expected=lambda x,y,w,h:rgb((x+.5)/w)
        check('axial',axial,function,expected)
        check('inherited-shading',axial,function,expected,inherited=True)
        check('axial-reversed',axial.replace('[0 0 18 0]','[18 0 0 0]'),function,lambda x,y,w,h:rgb(1-(x+.5)/w))
        check('axial-vertical',axial.replace('[0 0 18 0]','[0 0 0 12]'),function,lambda x,y,w,h:rgb(1-(y+.5)/h))
        check('exponent-two',axial,function.replace('/N 1','/N 2'),lambda x,y,w,h:rgb(((x+.5)/w)**2))
        check('clipped',axial,function,lambda x,y,w,h:expected(x,y,w,h)if x<12 else(128,)*3,content='0 0 9 12 re W n /Sh sh')
        check('bbox',axial.replace('/Function','/BBox [0 0 9 12] /Function'),function,lambda x,y,w,h:expected(x,y,w,h)if x<12 else(128,)*3)
        check('translated',axial,function,lambda x,y,w,h:rgb((x+.5-8)/w)if x>=8 else(128,)*3,content='1 0 0 1 6 0 cm /Sh sh')
        check('scaled',axial,function,lambda x,y,w,h:rgb((x+.5)/12)if x<12 else(128,)*3,content='.5 0 0 1 0 0 cm /Sh sh')
        short=axial.replace('[0 0 18 0]','[6 0 12 0]')
        for left,right in ((False,False),(True,False),(False,True),(True,True)):
            shade=short.replace('/Function',f'/Extend [{str(left).lower()} {str(right).lower()}] /Function')
            check('extend-'+str(count),shade,function,
                  lambda x,y,w,h:rgb(max(0,min(1,(x+.5-8)/8)))if(8<=x<16 or(x<8 and left)or(x>=16 and right))else(128,)*3)
        # Blend unrounded source and .5 background, then quantize the result once.
        check('opacity',axial,function,lambda x,y,w,h:tuple(int(c*.5+63.75+.5)for c in (255*(1-(x+.5)/w),0,255*(x+.5)/w)),content='/Half gs /Sh sh')
        gray='<< /FunctionType 2 /Domain [0 1] /N 1 >>'
        check('gray-defaults',axial.replace('DeviceRGB','DeviceGray'),gray,lambda x,y,w,h:(int((x+.5)/w*255+.5),)*3)
        check('function-range',axial,function.replace('/N 1','/Range [.25 .75 0 0 .25 .75] /N 1'),
              lambda x,y,w,h:tuple(int(max(.25,min(.75,c/255))*255+.5)if i!=1 else 0 for i,c in enumerate(expected(x,y,w,h))))
        check('indirect-coords',axial.replace('[0 0 18 0]','7 0 R'),function,expected,extra={7:'[0 0 18 0]'})
        check('indirect-n',axial,function.replace('/N 1','/N 7 0 R'),expected,extra={7:'1'})
        check('degenerate-axis',axial.replace('[0 0 18 0]','[0 0 0 0]'),function,lambda x,y,w,h:(128,)*3)
        radial='<< /ShadingType 3 /ColorSpace /DeviceRGB /Coords [9 6 0 9 6 6] /Extend [true true] /Function 6 0 R >>'
        check('radial',radial,function,lambda x,y,w,h:rgb(min(1,math.hypot((x+.5)*.75-9,12-(y+.5)*.75-6)/6)))
        check('radial-reversed',radial.replace('[9 6 0 9 6 6]','[9 6 6 9 6 0]'),function,
              lambda x,y,w,h:rgb(max(0,1-math.hypot((x+.5)*.75-9,12-(y+.5)*.75-6)/6)))
        check('cmyk',axial.replace('DeviceRGB','DeviceCMYK'),function.replace('[1 0 0]','[0 1 1 0]').replace('[0 0 1]','[1 1 0 0]'),expected)
        check('fractional-exponent',axial,function.replace('/N 1','/N .5'),lambda x,y,w,h:rgb(math.sqrt((x+.5)/w)))
        check('reversed-domain',axial.replace('/Function','/Domain [1 0] /Function'),function,lambda x,y,w,h:rgb(1-(x+.5)/w))
        check('rotated-ctm',axial,function,lambda x,y,w,h:rgb((h-y-.5)/w),content='0 1 -1 0 18 0 cm /Sh sh')
        check('nonunit-domain',axial.replace('DeviceRGB','DeviceGray').replace('/Function','/Domain [2 3] /Function'),
              '<< /FunctionType 2 /Domain [2 3] /C0 [-2] /C1 [-1] /N 1 >>',lambda x,y,w,h:(int((x+.5)/w*255+.5),)*3)
        check('inverse-function',axial.replace('DeviceRGB','DeviceGray').replace('/Function','/Domain [1 2] /Function'),
              '<< /FunctionType 2 /Domain [1 2] /N -1 >>',lambda x,y,w,h:(int(255/(1+(x+.5)/w)+.5),)*3)
        check('no-radial-extension',radial.replace('/Extend [true true]',''),function,
              lambda x,y,w,h:rgb(math.hypot((x+.5)*.75-9,12-(y+.5)*.75-6)/6)if math.hypot((x+.5)*.75-9,12-(y+.5)*.75-6)<6 else(128,)*3)
        form_content=b'/Sh sh'
        form=f'<< /Subtype /Form /BBox [0 0 9 12] /Resources << /Shading << /Sh 5 0 R >> >> /Length {len(form_content)} >>\nstream\n'.encode()+form_content+b'\nendstream'
        page='<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Resources << /XObject << /F 8 0 R >> >> >>'
        check('form-local-shading',axial,function,lambda x,y,w,h:expected(x,y,w,h)if x<12 else(128,)*3,
              content='/F Do',extra={3:page,8:form})
        for label,shade,fn in (
            ('negative-radius',radial.replace('9 6 0 9','9 6 -1 9'),function),
            ('wrong-coords',axial.replace('[0 0 18 0]','[0 0 18]'),function),
            ('bad-extend',axial.replace('/Function','/Extend [1 false] /Function'),function),
            ('bad-domain',axial.replace('/Function','/Domain [-1 1] /Function'),function),
            ('bad-function-domain',axial,function.replace('/Domain [0 1]','/Domain [1 0]')),
            ('unsupported-function',axial,function.replace('/FunctionType 2','/FunctionType 4')),
            ('bad-output-count',axial,function.replace('/C0 [1 0 0]','/C0 [1]')),
            ('bad-exponent',axial,function.replace('/N 1','/N -1')),
            ('cyclic-n',axial,function.replace('/N 1','/N 7 0 R')),
            ('unsupported-type',axial.replace('/ShadingType 2','/ShadingType 7'),function),
        ):
            check(label,shade,fn,reject=True,extra={7:'7 0 R'})
        # Wider native-resolution comparisons keep renderer quantization below
        # one channel unit for axial shading. Nonlinear reference approximation differs
        # by up to two units; all analytic tests above still require exact pixels.
        # No resizing or alignment is applied to either image.
        check('reference-axial',axial.replace('[0 0 18 0]','[0 0 450 0]'),function,size=(450,300),reference=True)
        check('reference-radial',radial.replace('[9 6 0 9 6 6]','[225 150 0 225 150 150]'),function,lambda x,y,w,h:rgb(min(1,math.hypot((x+.5)*.75-225,300-(y+.5)*.75-150)/150)),size=(450,300),reference=True,reference_limit=2)
        # The primary rasterizer leaves a boundary-color rim when a radial
        # gradient extends beyond the page. Keep full-edge analytic coverage
        # and use the second rasterizer for this comparison.
        check('reference-radial-page-clipped',radial.replace('[9 6 0 9 6 6]','[225 150 0 225 150 300]'),function,
              lambda x,y,w,h:rgb(min(1,math.hypot((x+.5)*.75-225,300-(y+.5)*.75-150)/300)),
              size=(450,300),reference=True,reference_limit=2,secondary=True)
        check('reference-exponent',axial.replace('[0 0 18 0]','[0 0 450 0]'),function.replace('/N 1','/N 2'),lambda x,y,w,h:rgb(((x+.5)/w)**2),size=(450,300),reference=True,reference_limit=2)
    print(f'PDF shadings: {count} passed')


if __name__=='__main__':main()
