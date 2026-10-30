#!/usr/bin/env python3
"""Check sampled color functions and two-input function-based shading."""
import base64
from pathlib import Path
import math
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_shading import shading_pdf
from test_pdf_lzw import literals
from audit_pdf_corpus import read_ppm


def packed(values,bits):
    number=0
    for value in values:number=(number<<bits)|value
    padding=(-len(values)*bits)%8
    return (number<<padding).to_bytes((len(values)*bits+7)//8,'big')


def sampled(values,bits=8,size=(2,),components=3,filters='',attrs='',raw=None):
    data=packed(values,bits)if raw is None else raw
    domain=' '.join('0 1'for _ in size);ranges=' '.join('0 1'for _ in range(components))
    return f'<< /FunctionType 0 /Domain [{domain}] /Range [{ranges}] /Size [{" ".join(map(str,size))}] /BitsPerSample {bits} /Length {len(data)} {filters} {attrs} >>\nstream\n'.encode()+data+b'\nendstream'


AXIAL='<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Function 6 0 R >>'
PLANE='<< /ShadingType 1 /ColorSpace /DeviceRGB /Matrix [18 0 0 12 0 0] /Function 6 0 R >>'


def main():
    probe=str(Path(sys.argv[1]).resolve());count=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-sampled-')as directory:
        def check(name,fn,expected=None,shade=AXIAL,content='/Sh sh',extra=None,reject=False,size=(18,12),reference=False,secondary=False):
            nonlocal count
            count+=1;path=Path(directory)/(name+'.pdf');path.write_bytes(shading_pdf(shade,fn,content,size,extra))
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert run.returncode==(3 if reject else 0),(name,run.stderr)
            if reject:return
            w,h,pages=struct.unpack('=III',run.stdout[:12]);assert(w,h,pages)==(round(size[0]*4/3),round(size[1]*4/3),1)
            if expected:
                wanted=bytes(c for y in range(h)for x in range(w)for c in (*expected(x,y,w,h),255))
                assert run.stdout[12:]==wanted,(name,next((i,a,b)for i,(a,b)in enumerate(zip(run.stdout[12:],wanted))if a!=b))
            if reference and '--reference'in sys.argv:
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
                assert error<=2,(name,'reference differs',error)
        rgb=lambda t:(int(255*(1-t)+.5),0,int(255*t+.5))
        gradient=lambda x,y,w,h:rgb((x+.5)/w)
        color=lambda u,v:tuple(int(c*255+.5)for c in ((1-u)*(1-v)+u*v,u,v))
        plane=lambda x,y,w,h:color((x+.5)/w,1-(y+.5)/h)
        for bits in (1,2,4,8,12,16,24,32):
            maximum=(1<<bits)-1
            values=(maximum,0,0,0,0,maximum)
            check('axial-'+str(bits),sampled(values,bits),gradient)
            data=zlib.compress(packed(values,bits))
            check('flate-'+str(bits),sampled(values,bits,filters='/Filter /FlateDecode',raw=data),gradient)
            values=(maximum,0,0,0,maximum,0,0,0,maximum,maximum,maximum,maximum)
            check('plane-'+str(bits),sampled(values,bits,size=(2,2)),plane,shade=PLANE)
            # Non-byte-aligned odd sample counts have no row padding.
            check('odd-samples-'+str(bits),sampled((0,maximum,0),bits,size=(3,),components=1),
                  lambda x,y,w,h:(int(255*(2*(x+.5)/w if x<w/2 else 2*(1-(x+.5)/w))+.5),)*3,
                  shade=AXIAL.replace('DeviceRGB','DeviceGray'))
        for bits in (12,16,24,32):
            check('low-bit-precision-'+str(bits),sampled((0,1),bits,components=1,attrs=f'/Decode [0 {(1<<bits)-1}]'),
                  lambda x,y,w,h:(int((x+.5)/w*255+.5),)*3,shade=AXIAL.replace('DeviceRGB','DeviceGray'))
        values=(255,0,0,0,0,255);data=packed(values,8)
        for name,raw,filters in (('lzw',literals(data,1),'/Filter /LZWDecode'),('chain',base64.a85encode(zlib.compress(data))+b'~>','/Filter [/ASCII85Decode /FlateDecode]')):
            check(name,sampled(values,raw=raw,filters=filters),gradient)
        check('encode-reverse',sampled(values,attrs='/Encode [1 0]'),lambda x,y,w,h:rgb(1-(x+.5)/w))
        check('encode-clamp',sampled(values,attrs='/Encode [-1 2]'),lambda x,y,w,h:rgb(max(0,min(1,3*(x+.5)/w-1))))
        check('decode-reverse',sampled(values,attrs='/Decode [1 0 1 0 1 0]'),lambda x,y,w,h:tuple(255-c for c in gradient(x,y,w,h)))
        check('extra-stream-bytes',sampled(values,raw=data+b'extra'),gradient)
        check('size-one',sampled((128,),size=(1,),components=1),lambda x,y,w,h:(128,)*3,shade=AXIAL.replace('DeviceRGB','DeviceGray'))
        fn=sampled((255,0,0,0,255,0,0,0,255,255,255,255),size=(2,2))
        check('matrix-domain',fn,lambda x,y,w,h:color((x+.5)/12,(16-y-.5)/8)if x<12 and y>=8 else(128,)*3,
              shade=PLANE.replace('[18 0 0 12 0 0]','[9 0 0 6 0 0]'))
        check('matrix-rotation',fn,lambda x,y,w,h:color(1-(y+.5)/h,1-(x+.5)/w),shade=PLANE.replace('[18 0 0 12 0 0]','[0 12 -18 0 18 0]'))
        check('matrix-ctm',fn,lambda x,y,w,h:color((x+.5)/12,1-(y+.5)/h)if x<12 else(128,)*3,shade=PLANE,content='.5 0 0 1 0 0 cm /Sh sh')
        check('bbox',fn,lambda x,y,w,h:plane(x,y,w,h)if x<12 else(128,)*3,shade=PLANE.replace('/Function','/BBox [0 0 9 12] /Function'))
        check('sh-ignores-background',fn,lambda x,y,w,h:color((x+.5)/12,(16-y-.5)/8)if x<12 and y>=8 else(128,)*3,
              shade=PLANE.replace('[18 0 0 12 0 0]','[9 0 0 6 0 0]').replace('/Function','/Background [1 0 0] /Function'))
        check('default-matrix',fn,lambda x,y,w,h:color((x+.5)*.75,12-(y+.5)*.75)if x==0 and y==15 else(128,)*3,
              shade=PLANE.replace('/Matrix [18 0 0 12 0 0]',''))
        check('singular-matrix',fn,lambda x,y,w,h:(128,)*3,shade=PLANE.replace('[18 0 0 12 0 0]','[0 0 0 0 0 0]'))
        check('bad-matrix-cleanup',fn,shade=PLANE.replace('[18 0 0 12 0 0]','[18 0 0 12]'),reject=True)
        scalar=[sampled(v,bits=1,size=(2,2),components=1)for v in ((1,0,0,1),(0,1,0,1),(0,0,1,1))]
        check('sampled-component-array','[7 0 R 8 0 R 9 0 R]',plane,shade=PLANE,extra=dict(zip((7,8,9),scalar)))
        check('sampled-stitch','<< /FunctionType 3 /Domain [0 1] /Functions [7 0 R] /Bounds [] /Encode [1 0] >>',
              lambda x,y,w,h:rgb(1-(x+.5)/w),extra={7:sampled(values)})
        check('degenerate-table-axis',sampled((0,255),size=(1,2),components=1),lambda x,y,w,h:(int((1-(y+.5)/h)*255+.5),)*3,
              shade=PLANE.replace('DeviceRGB','DeviceGray'))
        check('plane-reverse-encode',sampled((255,0,0,0,255,0,0,0,255,255,255,255),size=(2,2),attrs='/Encode [1 0 0 1]'),
              lambda x,y,w,h:color(1-(x+.5)/w,1-(y+.5)/h),shade=PLANE)
        check('decode-before-range',sampled(values,attrs='/Decode [-1 2 0 1 -1 2]'),lambda x,y,w,h:tuple(int(max(0,min(1,v))*255+.5)for v in (2-3*(x+.5)/w,0,3*(x+.5)/w-1)))
        check('cmyk',sampled((0,255,255,0,255,255,0,0),components=4),gradient,shade=AXIAL.replace('DeviceRGB','DeviceCMYK'))
        for name,broken in (
            ('zero-size',sampled(values).replace(b'/Size [2]',b'/Size [0]')),
            ('huge-size',sampled(values).replace(b'/Size [2]',b'/Size [2147483647]')),
            ('fractional-size',sampled(values).replace(b'/Size [2]',b'/Size [1.5]')),
            ('bad-bits',sampled(values,bits=3,raw=data)),
            ('short-data',sampled(values,raw=data[:-1])),
            ('missing-range',sampled(values).replace(b'/Range [0 1 0 1 0 1]',b'')),
            ('wrong-domain-arity',sampled(values).replace(b'/Domain [0 1]',b'/Domain [0 1 0 1]')),
            ('cubic-order',sampled(values,attrs='/Order 3')),
            ('short-decode',sampled(values,attrs='/Decode [0 1]')),
        ):check(name,broken,reject=True)
        check('partial-tree-cleanup','[7 0 R 8 0 R 9 0 R]',shade=PLANE,
              extra={7:scalar[0],8:b'<< /FunctionType 4 /Domain [0 1 0 1] /Range [0 1] >>',9:scalar[2]},reject=True)
        check('reference-low-32-bits',sampled((0,1),32,components=1,attrs='/Decode [0 4294967295]'),
              lambda x,y,w,h:(int((x+.5)/w*255+.5),)*3,
              shade=AXIAL.replace('DeviceRGB','DeviceGray').replace('[0 0 18 0]','[0 0 450 0]'),size=(450,300),reference=True)
        check('reference-sampled',sampled(values),gradient,shade=AXIAL.replace('[0 0 18 0]','[0 0 450 0]'),size=(450,300),reference=True)
        # The primary comparison tool uses a coarser mesh for this nonlinear
        # two-input surface. Compare to the secondary rasterizer as well as the
        # exact analytic pixels, without increasing the two-unit oracle bound.
        check('reference-plane',fn,plane,shade=PLANE.replace('[18 0 0 12 0 0]','[450 0 0 300 0 0]'),size=(450,300),reference=True,secondary=True)
    print(f'PDF sampled functions: {count} passed')


if __name__=='__main__':main()
