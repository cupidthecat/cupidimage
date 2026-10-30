#!/usr/bin/env python3
"""Check continuous Coons/tensor mappings and parameter-priority fold selection."""
from pathlib import Path
import math
import random
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm

ORDER=[(0,0),(0,1),(0,2),(0,3),(1,3),(2,3),(3,3),(3,2),(3,1),(3,0),(2,0),(1,0),(1,1),(1,2),(2,2),(2,1)]
COLORS=[(0,0,0),(0,1,0),(1,1,0),(1,0,0)]


def control_points(kind=6,transform=None):
    return [(i/3,j/3) if transform is None else transform(i,j) for i,j in ORDER[:12 if kind==6 else 16]]


def records(patches,kind=6,coordinate=8,component=8,flag=8,pad=0):
    data=bytearray()
    for edge,points,colors in patches:
        fields=[(edge,flag)]
        for point in points[4 if edge&3 else 0:]:
            fields += [(round(value*((1<<coordinate)-1)),coordinate) for value in point]
        for color in colors[2 if edge&3 else 0:]:
            fields += [(round(value*((1<<component)-1)),component) for value in color]
        bits=''.join(format(value,'0'+str(width)+'b')[-width:] for value,width in fields)
        bits+=str(pad)*((-len(bits))%8)
        data+=int(bits,2).to_bytes(len(bits)//8,'big')
    return bytes(data)


def fixture(kind=6,patches=None,coordinate=8,component=8,flag=8,pad=0,space='/DeviceRGB',decode=None,function=None,attrs='',content='/Sh sh',extra=None,resources='',compressed=False,raw=None):
    patches=[(0,control_points(kind),COLORS)] if patches is None else patches
    samples=len(patches[0][2][0]) if patches else 3
    decode=decode or [0,18,0,12]+[v for _ in range(samples) for v in (0,1)]
    data=records(patches,kind,coordinate,component,flag,pad) if raw is None else raw
    if compressed:data=zlib.compress(data)
    fields=f'/ShadingType {kind} /ColorSpace {space} /BitsPerCoordinate {coordinate} /BitsPerComponent {component} /BitsPerFlag {flag} /Decode ['+' '.join(map(str,decode))+'] '+attrs
    objects={}
    if function:fields+=' /Function 8 0 R';objects[8]=function
    if compressed:fields+=' /Filter /FlateDecode'
    objects[5]=stream(data,fields);objects.update(extra or {})
    return document('/MediaBox [0 0 18 12] /Resources << /Shading << /Sh 5 0 R >> /ExtGState << /Half << /ca .5 >> >> '+resources+' >>',stream=content,extra=objects)


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=refs=0
    white=(255,255,255);red=(255,0,0)
    byte=lambda value:math.floor(max(0,min(1,value))*255+.5)
    gradient=lambda x,y:(byte((x+.5)/24),byte(1-(y+.5)/16),0)
    with tempfile.TemporaryDirectory(prefix='cupidimage-patch-') as tmp:
        path=Path(tmp)/'case.pdf'
        def check(name,pdf,expected=None,reject=False,reference=False):
            nonlocal checks,refs
            path.write_bytes(pdf);result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=30)
            assert result.returncode==(3 if reject else 0),(name,result.returncode,result.stderr)
            checks+=1
            if reject:return
            assert struct.unpack('=III',result.stdout[:12])==(24,16,1)
            want=bytes(v for y in range(16) for x in range(24) for v in (*expected(x,y),255))
            assert result.stdout[12:]==want,(name,'analytic differs',next((i,a,b) for i,(a,b) in enumerate(zip(result.stdout[12:],want)) if a!=b))
            if '--reference' in sys.argv and reference:
                prefix=Path(tmp)/'reference';run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=30)
                assert run.returncode==0,run.stderr;w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                actual=bytes(v for i,v in enumerate(result.stdout[12:]) if i%4!=3)
                assert (w,h)==(24,16) and max(abs(a-b) for a,b in zip(actual,rgb))<=1,(name,'independent differs',max(abs(a-b) for a,b in zip(actual,rgb)))
                refs+=1
        for kind in (6,7):
            points=control_points(kind)
            check('linear-'+str(kind),fixture(kind),gradient)
            solid=[(1,0,0)]*4
            check('solid-'+str(kind),fixture(kind,patches=[(0,points,solid)]),lambda x,y:red,reference=True)
            quadratic=control_points(kind,lambda i,j:([0,0,1/3,1][i],j/3))
            check('quadratic-inverse-'+str(kind),fixture(kind,patches=[(0,quadratic,COLORS)]),lambda x,y:(byte(math.sqrt((x+.5)/24)),gradient(x,y)[1],0))
            fold=control_points(kind,lambda i,j:(i/3,[0,1,1,0][j]))
            green=[(0,0,0),(0,1,0),(0,1,0),(0,0,0)]
            check('fold-largest-v-'+str(kind),fixture(kind,patches=[(0,fold,green)]),lambda x,y:(0,byte((1+math.sqrt(1-4*(1-(y+.5)/16)/3))/2),0) if 1-(y+.5)/16<=.75 else white)
            fold=control_points(kind,lambda i,j:([0,1,1,0][i],j/3))
            horizontal=[(0,0,0),(0,0,0),(1,0,0),(1,0,0)]
            check('fold-largest-u-'+str(kind),fixture(kind,patches=[(0,fold,horizontal)]),lambda x,y:(byte((1+math.sqrt(1-4*(x+.5)/24/3))/2),0,0) if (x+.5)/24<=.75 else white)
            diagonal_fold=control_points(kind,lambda i,j:(i/3,([0,120,120,0][j]+12*i*j)/255))
            def diagonal(x,y):
                u=(x+.5)/24;value=1-(y+.5)/16;a=360/255;b=108/255;t=a+b*u
                discriminant=t*t-4*a*value
                if discriminant<0:return white
                root=math.sqrt(discriminant);v=(t+root)/(2*a)
                if v>1:v=2*value/(t+root)
                return (byte(u),byte(v),0) if 0<=v<=1 else white
            check('diagonal-fold-'+str(kind),fixture(kind,patches=[(0,diagonal_fold,COLORS)]),diagonal)
            bulge=control_points(kind,lambda i,j:(([63,106,149,192][i] if j in (0,3) else [0,85,170,255][i])/255,j/3))
            def curved(x,y):
                v=1-(y+.5)/16;left=63/255*(1-3*v*(1-v));u=((x+.5)/24-left)/(1-2*left)
                return (byte(u),byte(v),0) if 0<=u<=1 else white
            check('curved-boundary-'+str(kind),fixture(kind,patches=[(0,bulge,COLORS)]),curved)
            scalar=[(0,),(0,),(1,),(1,)]
            function='<< /FunctionType 2 /Domain [0 1] /C0 [0 0 0] /C1 [1 0 0] /N 2 >>'
            check('inverse-then-function-'+str(kind),fixture(kind,patches=[(0,quadratic,scalar)],function=function),lambda x,y:(gradient(x,y)[0],0,0))
            check('indexed-base-'+str(kind),fixture(kind,patches=[(0,points,scalar)],space='[/Indexed /DeviceRGB 1 <ff00000000ff>]'),lambda x,y:(byte(1-(x+.5)/24),0,gradient(x,y)[0]))
            check('later-patch-'+str(kind),fixture(kind,patches=[(0,points,solid),(0,points,[(0,0,1)]*4)]),lambda x,y:(0,0,255),reference=True)
            check('overlap-opacity-'+str(kind),fixture(kind,patches=[(0,points,solid),(0,points,[(0,0,1)]*4)],content='/Half gs /Sh sh'),lambda x,y:(128,128,255))
            for coordinate in (1,2,4,8,12,16,24,32):
                for component in (1,2,4,8,12,16):
                    check(f'bits-{kind}-{coordinate}-{component}',fixture(kind,patches=[(0,points,solid)],coordinate=coordinate,component=component,flag=2,pad=1),lambda x,y:red,reference=True)
            for edge in (1,2,3):
                origin=6 if edge==3 else 0
                base=control_points(kind,lambda i,j:(3*i/255,(origin+2*j)/255))
                second=control_points(kind,lambda i,j:((3*j if edge==1 else 9+3*i if edge==2 else 9-3*j)/255,(6+2*i if edge==1 else 6-2*j if edge==2 else 6-2*i)/255))
                full=[(0,base,solid),(edge,second,[(0,0,1),(0,0,1),(1,0,0),(1,0,0)])]
                check(f'unclipped-edge-{kind}-{edge}',fixture(kind,patches=full,decode=[0,255,0,255,0,1,0,1,0,1]),lambda x,y:red if (y>=8 if edge==2 else x<12) else white)
                for flag in (2,4,8):
                    patch=[(0,base,solid),(edge,second,[(0,0,1),(0,0,1),(1,0,0),(1,0,0)])]
                    check(f'edge-{kind}-{edge}-{flag}',fixture(kind,patches=patch,flag=flag,pad=1,decode=[0,255,0,255,0,1,0,1,0,1],content=('0 0 18 6 re W n /Sh sh' if edge==2 else '0 0 9 12 re W n /Sh sh')),lambda x,y:red if (y>=8 if edge==2 else x<12) else white,reference=True)
            for high in (12,252):
                check('unused-flag-bits-'+str(kind)+str(high),fixture(kind,patches=[(high,points,solid)],flag=4 if high==12 else 8),lambda x,y:red)
            check('bbox-'+str(kind),fixture(kind,patches=[(0,points,solid)],attrs='/BBox [0 0 9 12]'),lambda x,y:red if x<12 else white,reference=True)
            check('compressed-'+str(kind),fixture(kind,patches=[(0,points,solid)],compressed=True),lambda x,y:red,reference=True)
            pattern=b'<< /PatternType 2 /Shading 5 0 R >>'
            check('pattern-array-'+str(kind),fixture(kind,patches=[(0,points,solid)],content='/PCS cs /P scn 0 0 18 12 re f',extra={7:pattern},resources='/ColorSpace << /PCS [/Pattern] >> /Pattern << /P 7 0 R >>'),lambda x,y:red,reference=True)
            check('mirrored-'+str(kind),fixture(kind,content='-1 0 0 1 18 0 cm /Sh sh'),lambda x,y:(byte(1-(x+.5)/24),gradient(x,y)[1],0))
            check('rotated-'+str(kind),fixture(kind,content='0 .6666666666666666 -1.5 0 18 0 cm /Sh sh'),lambda x,y:(gradient(x,y)[1],byte(1-(x+.5)/24),0))
            scaled=lambda x,y:(byte((x+.5)/12),byte((15.5-y)/8),0) if x<12 and y>=8 else white
            check('scaled-'+str(kind),fixture(kind,content='.5 0 0 .5 0 0 cm /Sh sh'),scaled)
            mapped=b'<< /PatternType 2 /Shading 5 0 R /Matrix [.5 0 0 .5 0 0] >>'
            check('pattern-matrix-'+str(kind),fixture(kind,content='/Pattern cs /P scn 0 0 18 12 re f',extra={7:mapped},resources='/Pattern << /P 7 0 R >>'),scaled)
            gray=[(0,),(1,),(1,),(0,)]
            check('gray-'+str(kind),fixture(kind,patches=[(0,points,gray)],space='/DeviceGray'),lambda x,y:(gradient(x,y)[1],)*3)
            cmyk=[(0,1,1,0),(0,1,1,0),(1,0,1,0),(1,0,1,0)]
            check('cmyk-'+str(kind),fixture(kind,patches=[(0,points,cmyk)],space='/DeviceCMYK'),lambda x,y:(byte(1-(x+.5)/24),gradient(x,y)[0],0))
            check('degenerate-'+str(kind),fixture(kind,patches=[(0,[(0,0)]*len(points),solid)]),lambda x,y:white)
            for kwargs in ({'patches':[]},{'patches':[(1,points,solid)]},{'raw':records([(0,points,solid)],kind)[:-1]},{'coordinate':3},{'component':3},{'flag':1}):
                check('invalid-'+str(kind)+str(kwargs),fixture(kind,**kwargs),reject=True)
        rng=random.Random(798)
        pairs=[(0,0),(255,255),(0,255),(255,0),(64,64),(1,254),(0,1),(254,255)]+[(rng.randrange(256),rng.randrange(256)) for _ in range(12)]
        def inverse_curve(a,b,value):
            low,high=0.,1.
            for _ in range(60):
                u=(low+high)*.5;v=3*u*(1-u)**2*a+3*u*u*(1-u)*b+u**3
                if v<value:low=u
                else:high=u
            return (low+high)*.5
        for index,(a,b) in enumerate(pairs):
            c,d=pairs[-index-1]
            px=[0,a/255,b/255,1];py=[0,c/255,d/255,1]
            columns=[byte(inverse_curve(px[1],px[2],(x+.5)/24)) for x in range(24)]
            rows=[byte(inverse_curve(py[1],py[2],1-(y+.5)/16)) for y in range(16)]
            for kind in (6,7):
                control=control_points(kind,lambda i,j:(px[i],py[j]))
                check(f'separable-inverse-{kind}-{index}',fixture(kind,patches=[(0,control,COLORS)]),lambda x,y:(columns[x],rows[y],0))
        a=252/255
        tensor=control_points(7,lambda i,j:(i/3+(28/255 if i in (1,2) and j in (1,2) else 0),j/3))
        def warped(x,y):
            v=1-(y+.5)/16;k=a*v*(1-v);p=(x+.5)/24
            u=2*p/(1+k+math.sqrt((1+k)**2-4*k*p))
            return byte(u),byte(v),0
        check('tensor-interior-mapping',fixture(7,patches=[(0,tensor,COLORS)]),warped)
    print(f'PDF patch meshes: {checks} checks passed; {refs} independent comparisons')


if __name__=='__main__':main()
