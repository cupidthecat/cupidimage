#!/usr/bin/env python3
"""Render calculator color functions and reject malformed or unsafe programs."""
import base64
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_shading import shading_pdf
from test_pdf_sampled_functions import AXIAL, PLANE
from audit_pdf_corpus import read_ppm


def calculator(code,inputs=1,components=3,filtered=False,attrs=''):
    data=code.encode()if isinstance(code,str)else code
    filters=''
    if filtered:
        data=base64.a85encode(zlib.compress(data))+b'~>'
        filters='/Filter [/ASCII85Decode /FlateDecode]'
    return f'<< /FunctionType 4 /Domain [{"0 1 "*inputs}] /Range [{"0 1 "*components}] /Length {len(data)} {filters} {attrs} >>\nstream\n'.encode()+data+b'\nendstream'


def main():
    probe=str(Path(sys.argv[1]).resolve());count=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-calculator-')as directory:
        def check(name,fn,expected=None,shade=AXIAL,extra=None,reject=False,reference=False,boundaries=()):
            nonlocal count
            count+=1;path=Path(directory)/(name+'.pdf');path.write_bytes(shading_pdf(shade,fn,extra=extra))
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert run.returncode==(3 if reject else 0),(name,run.stderr)
            if reject:return
            w,h,pages=struct.unpack('=III',run.stdout[:12]);assert(w,h,pages)==(24,16,1)
            wanted=bytes(c for y in range(h)for x in range(w)for c in (*expected(x,y,w,h),255))
            assert run.stdout[12:]==wanted,(name,next((i,a,b)for i,(a,b)in enumerate(zip(run.stdout[12:],wanted))if a!=b))
            if reference and '--reference'in sys.argv:
                prefix=Path(directory)/'reference'
                subprocess.run(['pdftoppm','-singlefile','-r','96','-aaVector','no',str(path),str(prefix)],capture_output=True,check=True,timeout=15)
                rw,rh,rgb=read_ppm(prefix.with_suffix('.ppm'))
                assert(rw,rh)==(w,h)
                actual=bytes(c for i,c in enumerate(run.stdout[12:])if i%4!=3)
                error=max(abs(a-b)for i,(a,b)in enumerate(zip(actual,rgb)) if all(abs((i//3)%w+.5-edge)>1 for edge in boundaries));assert error<=2,(name,'reference',error)
        gradient=lambda x,y,w,h:(int(255*(1-(x+.5)/w)+.5),0,int(255*(x+.5)/w+.5))
        for filtered in (False,True):
            check('gradient-'+str(filtered),calculator('{ 1 exch sub dup 0 exch 1 exch sub }',filtered=filtered),gradient,reference=True)
        # Compare this plane to exact pixel centers: reference mesh tessellation
        # differs by up to 14 byte levels at this small native resolution.
        check('plane',calculator('{ 0 }',inputs=2),lambda x,y,w,h:(int((x+.5)/w*255+.5),int((1-(y+.5)/h)*255+.5),0),shade=PLANE)
        # Reference ramp caches interpolate across discontinuities. Compare the
        # interiors independently; the analytic check above covers every pixel.
        check('condition',calculator('{ .5 lt { 1 0 0 } { 0 0 1 } ifelse }'),lambda x,y,w,h:(255,0,0)if x<12 else(0,0,255),reference=True,boundaries=(6,12))
        check('nested-condition',calculator('{ dup .5 lt { .25 lt { 1 0 0 } { 0 1 0 } ifelse } { pop 0 0 1 } ifelse }'),
              lambda x,y,w,h:(255,0,0)if x<6 else(0,255,0)if x<12 else(0,0,255),reference=True,boundaries=(6,12))
        check('component-array','[7 0 R 8 0 R 9 0 R]',gradient,extra={7:calculator('{1 exch sub}',components=1),8:calculator('{pop 0}',components=1),9:calculator('{}',components=1)},reference=True)
        check('stitch-child','<< /FunctionType 3 /Domain [0 1] /Functions [7 0 R] /Bounds [] /Encode [0 1] >>',gradient,extra={7:calculator('{ 1 exch sub dup 0 exch 1 exch sub }')},reference=True)
        check('range-clamp',calculator('{ pop -1 2 .5 }'),lambda x,y,w,h:(0,255,128),reference=True)
        check('domain-map',calculator('{ dup 0 exch }').replace(b'/Domain [0 1 ]',b'/Domain [.25 .75]'),
              lambda x,y,w,h:(int((.25+.5*(x+.5)/w)*255+.5),0,int((.25+.5*(x+.5)/w)*255+.5)),shade=AXIAL.replace('/Function','/Domain [.25 .75] /Function'))
        for name,code in [('extra-output','{0 0 0}'),('short-output','{pop 0}'),('boolean-output','{pop true 0 0}'),
                          ('underflow','{pop pop}'),('unknown','{pop run}'),('divide-zero','{0 div 0 0}'),
                          ('bad-branch','{true {0 0} ifelse}'),('unclosed','{pop 0 0 0'),('exponent','{pop 1e0 0 0}')]:
            check(name,calculator(code),reject=True)
        check('missing-range',calculator('{0 0}').replace(b'/Range [0 1 0 1 0 1 ]',b''),reject=True)
        check('partial-load','[7 0 R 8 0 R 9 0 R]',extra={7:calculator('{}',components=1),8:calculator('{unknown}',components=1),9:calculator('{}',components=1)},reject=True)
    print(f'PDF calculator rendering: {count} checks passed')


if __name__=='__main__':main()
