#!/usr/bin/env python3
"""Check opt-in PDF path coverage, paint composition and Type 3 graphics."""
import argparse
import itertools
import math
from pathlib import Path
import struct
import subprocess
import tempfile
from test_pdf_pages import document
from test_pdf_forms import form
from test_pdf_type3 import fixture as type3_fixture,stream
from test_pdf_text_strokes import combined, polygon_mask
from fractions import Fraction

WIDTH,HEIGHT=48,40
RATIO=96 / 72

def box(x,y,w,h):
    return (x*RATIO,(30-y-h)*RATIO,(x+w)*RATIO,(30-y)*RATIO)

def area(bounds,x,y):
    a,b,c,d=bounds
    return max(0,min(c,x+1)-max(a,x))*max(0,min(d,y+1)-max(b,y))

def rectangle_mask(bounds):
    return bytes(min(255,math.floor(area(bounds,x,y)*256)) for y in range(HEIGHT) for x in range(WIDTH))

def stroke_mask(bounds,width):
    radius=(width*96/72 if width else 1)/2
    a,b,c,d=bounds
    outer=(a-radius,b-radius,c+radius,d+radius)
    inner=(a+radius,b+radius,c-radius,d-radius)
    return bytes(min(255,math.floor((area(outer,x,y)-area(inner,x,y))*256)) for y in range(HEIGHT) for x in range(WIDTH))

def colored(mask,color=(1,0,0),opacity=1):
    return bytes(v for coverage in mask for v in (*[max(0,min(255,math.floor(255-coverage*(1-c)*opacity+.5))) for c in color],255))

def fixture(content,fill=1,stroke=1,ais=False,mask=None,pattern=False,in_form=False,group=False):
    resources='/ExtGState << /E 6 0 R /M 7 0 R >> /XObject << /G 8 0 R >> /Pattern << /P 9 0 R >>'
    setup='/E gs '+('/M gs ' if mask is not None else '')+'1 0 0 rg 0 0 1 RG '
    if pattern:setup+='/Pattern cs /P scn '
    attrs='/BBox [0 0 36 30] /Resources << '+resources+' >>'
    if group:attrs+=' /Group << /S /Transparency /I true /CS /DeviceRGB >>'
    objects={6:f'<< /ca {fill} /CA {stroke} /AIS {str(ais).lower()} >>',
             7:'<< /SMask << /S /Luminosity /G 10 0 R >> >>',
             8:form(content,attrs),
             9:stream('1 0 0 rg 0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>'),
             10:form(f'{mask if mask is not None else 1} g 0 0 36 30 re f','/BBox [0 0 36 30] /Group << /S /Transparency /I true /CS /DeviceGray >>')}
    return document('/MediaBox [0 0 36 30] /Resources << '+resources+' >>',stream=setup+('/G Do' if in_form else content),extra=objects)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe',type=Path)
    parser.add_argument('allocator',type=Path)
    parser.add_argument('cli',type=Path,nargs='?')
    args=parser.parse_args();checks=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupid-graphics-aa-') as temp:
        path=Path(temp)/'case.pdf'
        def render(data,graphics=1,text=0,valid=True):
            nonlocal checks
            path.write_bytes(data)
            result=subprocess.run([str(args.probe.resolve()),str(path),'0',str(text),'0','0','0',str(graphics)],capture_output=True,timeout=30)
            assert result.returncode==(0 if valid else 3),(result.returncode,result.stderr)
            checks+=1
            if not valid:return None
            w,h,n=struct.unpack('<III',result.stdout[:12]);assert n==1
            return w,h,result.stdout[12:]
        def fault(data):
            nonlocal allocations
            path.write_bytes(data)
            result=subprocess.run([str(args.allocator.resolve()),str(path),'0','0','0','0','0','1'],capture_output=True,text=True,check=True,timeout=120)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        rectangles=[(12.1875,9.1875,6.375,12.375),(-.1875,-.1875,.375,.375),
                    (0,0,.1875,30),(35.8125,29.8125,.375,.375),(6,6,12,12)]
        for rect,opacity in itertools.product(rectangles,(.2,.5,1)):
            content=' '.join(map(str,rect))+' re f'
            expected=colored(rectangle_mask(box(*rect)),opacity=opacity)
            assert render(fixture(content,fill=opacity))==(WIDTH,HEIGHT,expected),(rect,opacity,'analytic rectangle')
        quarter=fixture('0 0 .1875 30 re f')
        assert render(quarter,graphics=0)[2]==b'\xff'*(WIDTH*HEIGHT*4)
        assert render(quarter,graphics=0,text=1)[2]==b'\xff'*(WIDTH*HEIGHT*4)
        assert render(quarter,graphics=1)[2]!=b'\xff'*(WIDTH*HEIGHT*4)
        for invalid in (-1,2):render(quarter,graphics=invalid,valid=False)
        rect=rectangles[0];shape=' '.join(map(str,rect))+' re '
        fmask=rectangle_mask(box(*rect))
        for width in (0,.375,1.125,6,30):
            expected=colored(stroke_mask(box(*rect),width),(0,0,1))
            assert render(fixture(f'{width} w '+shape+'S'))[2]==expected,(width,'analytic rectangle stroke')
        smask=stroke_mask(box(*rect),1.125)
        for ais,(fill,stroke),mask in itertools.product((False,True),((0,0),(.2,0),(.6,.4),(1,.4),(.6,1)),(None,.2,.6)):
            data=fixture('1.125 w '+shape+'B',fill=fill,stroke=stroke,ais=ais,mask=mask)
            assert render(data)[2]==combined(fmask,smask,fill,stroke,ais,1 if mask is None else mask),(ais,fill,stroke,mask,'combined paint')
        # Fill-rule selection must survive clipping and combined painting.
        outer='6.1875 6.1875 12.375 12.375 re '
        inner='9.1875 9.1875 6.375 6.375 re '
        a,b=box(6.1875,6.1875,12.375,12.375),box(9.1875,9.1875,6.375,6.375)
        ring=bytes(min(255,math.floor((area(a,x,y)-area(b,x,y))*256)) for y in range(HEIGHT) for x in range(WIDTH))
        assert render(fixture(outer+inner+'f'))[2]==colored(rectangle_mask(a))
        assert render(fixture(outer+inner+'f*'))[2]==colored(ring)
        clip=box(6.1875,6.1875,6.375,12.375)
        def intersection(a,b):
            return max(a[0],b[0]),max(a[1],b[1]),min(a[2],b[2]),min(a[3],b[3])
        clipped_outer,clipped_inner=intersection(a,clip),intersection(b,clip)
        clipped_ring=bytes(min(255,math.floor((area(clipped_outer,x,y)-area(clipped_inner,x,y))*256)) for y in range(HEIGHT) for x in range(WIDTH))
        prefix='6.1875 6.1875 6.375 12.375 re W n '
        assert render(fixture(prefix+outer+inner+'f'))[2]==colored(rectangle_mask(clipped_outer))
        assert render(fixture(prefix+outer+inner+'f*'))[2]==colored(clipped_ring)
        # Cubic shorthand and reversed traversal preserve the same boundary.
        curve='6 6 m 6 6 18 24 30 6 c h f'
        cubic=render(fixture(curve))[2]
        assert render(fixture('6 6 m 18 24 30 6 v h f'))[2]==cubic
        assert render(fixture('30 6 m 18 24 6 6 6 6 c h f'))[2]==cubic
        assert render(fixture('6 6 m 18 24 30 6 y h f'))[2]==render(fixture('6 6 m 18 24 30 6 30 6 c h f'))[2]
        fine=render(fixture('.0625 i '+curve))[2]
        assert fine!=cubic,'flatness must affect cubic tessellation'
        def covered_area(rgba):
            return sum(1 if rgba[i+1]==0 else (255-rgba[i+1])/256 for i in range(0,len(rgba),4))
        # Integral of (32-y(t))*x'(t) is exactly 1152/5 device pixels.
        assert abs(covered_area(fine)-1152/5)<abs(covered_area(cubic)-1152/5)
        for operator in ('B','B*','b','b*'):
            # Separate masks are an oracle for combined-paint routing, not the
            # geometry oracle used by the rectangle cases above.
            contour='6.1875 6.1875 m 18.5625 6.1875 l 18.5625 18.5625 l '
            if operator.startswith('b'):contour+='h '
            f=render(fixture(contour+('f*' if '*' in operator else 'f')))[2]
            s=render(fixture('1.125 w '+contour+'S'))[2]
            fm=bytes(255-f[i+1] for i in range(0,len(f),4));sm=bytes(255-s[i] for i in range(0,len(s),4))
            assert render(fixture('1.125 w '+contour+operator,fill=.6,stroke=.4))[2]==combined(fm,sm,.6,.4,False)
        # Rational polygon clipping checks affine fills independently of the
        # production line integrator. One rotated boundary differs by one level
        # at an exact quantization boundary; all other cases are byte-exact.
        for matrix in ((1,0,0,1,0,0),(.5,.75,-.75,.5,18,0),
                       (-1,0,.25,1,30,0),(1,.25,.125,1,0,0),(.5,0,0,1.5,3,0)):
            a,b,c,d,e,f=matrix
            points=[((a*x+c*y+e)*RATIO,(30-(b*x+d*y+f))*RATIO)
                    for x,y in ((6.1875,6.1875),(18.5625,6.1875),(18.5625,18.5625),(6.1875,18.5625))]
            mask=polygon_mask([tuple(Fraction(v) for v in p) for p in points],WIDTH,HEIGHT)
            expected=colored(mask)
            actual=render(fixture(' '.join(map(str,matrix))+' cm '+outer+'f'))[2]
            tolerance=1 if matrix==(.5,.75,-.75,.5,18,0) else 0
            assert max(abs(a-b) for a,b in zip(actual,expected))<=tolerance,(matrix,'rational affine fill')
        # ISO 32000-2 8.5.3.3.1: filled point subpaths enclose a device pixel.
        for x,y in ((6,6),(6.1875,6.1875),(-.1875,6),(36,6),(6,30)):
            px,py=math.floor(x*RATIO),math.floor((30-y)*RATIO)
            mask=bytearray(WIDTH*HEIGHT)
            if 0<=px<WIDTH and 0<=py<HEIGHT:mask[py*WIDTH+px]=255
            expected=colored(mask)
            for suffix in ('h','l'):
                point=f'{x} {y} m '+(f'{x} {y} l' if suffix=='l' else 'h')
                for graphics,operator in itertools.product((0,1),('f','f*')):
                    assert render(fixture(point+' '+operator),graphics=graphics)[2]==expected
                    assert render(fixture(point+' W n 0 0 36 30 re f'),graphics=graphics)[2]==expected
            for graphics in (0,1):
                assert render(fixture(f'{x} {y} m f'),graphics=graphics)[2]==b'\xff'*(WIDTH*HEIGHT*4)
        fault(fixture('6.1875 6.1875 m h f'))
        # Closed round points have analytic quarter-circle coverage at this phase.
        point=bytearray(WIDTH*HEIGHT)
        for x,y in ((1,37),(2,37),(1,38),(2,38)):point[y*WIDTH+x]=50
        assert render(fixture('1 J .75 w 1.5 1.5 m h S'))[2]==colored(point,(0,0,1))
        for cap in (0,2):assert render(fixture(f'{cap} J .75 w 1.5 1.5 m h S'))[2]==b'\xff'*(WIDTH*HEIGHT*4)
        # True geometric intersection: two disjoint quarters of the same pixel.
        assert render(fixture('0 0 .1875 30 re W n .375 0 .1875 30 re f'))[2]==b'\xff'*(WIDTH*HEIGHT*4)
        for group in (False,True):
            expected=render(fixture(shape+'f',fill=.6))[2]
            assert render(fixture(shape+'f',fill=.6,in_form=True,group=group))[2]==expected
        assert render(fixture(shape+'f',fill=.6,pattern=True))[2]==render(fixture(shape+'f',fill=.6))[2]
        for data in (fixture(shape+'f'),fixture('1.125 w '+shape+'B',fill=.6,stroke=.4,mask=.6),
                     fixture('6 6 12 12 re W n 1 j 1.125 w '+shape+'B*')):fault(data)
        # Text clipping must also be captured when only graphics AA is enabled.
        # The font's paint profile is independent of its clipping outline.
        from test_pdf_text_antialias import MAKERS
        for maker in MAKERS.values():
            data=maker('BT /F 30 Tf 7 Tr 20.1875 30.1875 Td (A) Tj ET '
                       '1 0 0 rg 0 0 120 90 re f')
            graphics_clip=render(data,graphics=1,text=0)
            assert graphics_clip==render(data,graphics=1,text=1)
            assert graphics_clip==render(data,graphics=0,text=1)
        # Type 3 charstrings execute the same graphic path coverage.
        content='1 0 0 rg BT /F 30 Tf 20.1875 30.1875 Td (A) Tj ET'
        actual=render(type3_fixture(content))[2]
        expected=render(document('/MediaBox [0 0 120 90]',stream='1 0 0 rg 20.1875 30.1875 15 21 re f'))[2]
        assert actual==expected,'Type 3 path coverage'
        assert render(type3_fixture(content),graphics=0)[2]!=actual
        fault(type3_fixture(content))
        if args.cli:
            from test_sixel import decode
            expected=render(fixture('1.125 w '+shape+'B',fill=.6,stroke=.4))[2]
            result=subprocess.run([str(args.cli.resolve()),'--sixel','--sixel-dither','none','--pdf-graphics-aa',str(path),'48','40'],capture_output=True,check=True,timeout=30)
            transparent,dimensions,pixels=decode(result.stdout)
            assert not transparent and dimensions==(WIDTH,HEIGHT)
            assert pixels==[tuple(expected[i+c]*100//255 for c in range(3)) for i in range(0,len(expected),4)]
            checks+=1
    print(f'PDF graphics antialias: {checks} page/API/CLI checks; {allocations} allocation checks')


if __name__=='__main__':
    main()
