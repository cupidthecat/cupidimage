#!/usr/bin/env python3
"""Check fractional text strokes, user-space widths and combined painting."""
import argparse
import itertools
import math
from pathlib import Path
import struct
import subprocess
import tempfile
from test_pdf_text_antialias import MAKERS, RATIO, rectangle_mask, transformed
from test_pdf_type3 import stream
from test_pdf_forms import form
from test_pdf_type1_charstrings import command, WIDTH as T1_WIDTH

WIDTH, HEIGHT = 160, 120
TM = (1, 0, 0, 1, 20.1875, 30.1875)


def text(mode=1, size=30, char='A', matrix=TM):
    return f'BT /F {size} Tf {mode} Tr '+ ' '.join(map(str,matrix))+f' Tm ({char}) Tj ET'


def fixture(kind, content, width=1.125, fill=1, stroke=1, ais=False, mask=None,
            pattern=False, in_form=False, stroke_color='0 0 1', **kwargs):
    resources = ('/Font << /F 5 0 R >> /ExtGState << /E 10 0 R /M 11 0 R >> '
                 '/Pattern << /P 21 0 R >> /XObject << /G 20 0 R >>')
    setup = '/E gs '+('/M gs ' if mask is not None else '')
    setup += f'1 0 0 rg {stroke_color} RG {width} w '
    if pattern:
        setup += '/Pattern CS /P SCN '
    body = setup+content
    objects = {
        3: ('<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] '
            '/Resources << '+resources+' >> >>').encode(),
        10: f'<< /ca {fill} /CA {stroke} /AIS {str(ais).lower()} >>',
        11: '<< /SMask << /S /Luminosity /G 22 0 R >> >>',
        20: form(body, '/BBox [0 0 120 90] /Resources << '+resources+' >>'),
        21: stream('0 0 1 rg 0 0 4 4 re f', '/Type /Pattern /PatternType 1 /PaintType 1 '
                   '/TilingType 1 /BBox [0 0 4 4] /XStep 4 /YStep 4 /Resources << >>'),
        22: form(f'{mask if mask is not None else 1} g 0 0 120 90 re f',
                 '/BBox [0 0 120 90] /Group << /S /Transparency /I true /CS /DeviceGray >>'),
    }
    return MAKERS[kind]('/G Do' if in_form else body, extra=objects, **kwargs)


def top_down(mask):
    return b''.join(mask[y*WIDTH:(y+1)*WIDTH] for y in reversed(range(HEIGHT)))


def stroke_rectangle(size=30, width=1.125, matrix=TM):
    _,_,_,_,tx,ty = matrix
    radius = width*(96/72)/2
    x0,x1 = (tx+1.125)*RATIO,(tx+1.125+size*.4)*RATIO
    y0,y1 = (90-ty-size*.6)*RATIO,(90-ty)*RATIO
    outer = (x0-radius,y0-radius,x1+radius,y1+radius)
    inner = (x0+radius,y0+radius,x1-radius,y1-radius)
    def area(box,x,y):
        left,top,right,bottom = box
        return max(0,min(right,(x+1))-max(left,x))*max(0,min(bottom,(y+1))-max(top,y))
    return bytes(min(255,math.floor((area(outer,x,y)-area(inner,x,y))*256))
                 for y in range(HEIGHT) for x in range(WIDTH))


def red_stroke(mask):
    return bytes(v for alpha in mask for v in (255,255-alpha,255-alpha,255))


def combined(fill_mask, stroke_mask, fill, stroke, ais, mask=1):
    result = bytearray()
    for f,s in zip(fill_mask,stroke_mask):
        af=f/255*fill
        alpha=s/255*stroke
        shape=s/255*(stroke if ais else 1)
        rgb=(1-alpha,1-alpha-(1-shape)*af,1-(1-shape)*af)
        result.extend(max(0,min(255,math.floor((1-mask*(1-c))*255+.5))) for c in rgb)
        result.append(255)
    return bytes(result)


def polygon_mask(points, width=WIDTH, height=HEIGHT):
    """Independent convex clipping and shoelace area for straight cap cases."""
    result=bytearray(width*height)
    for y in range(max(0,math.floor(min(p[1] for p in points))),min(height,math.ceil(max(p[1] for p in points)))):
        for x in range(max(0,math.floor(min(p[0] for p in points))),min(width,math.ceil(max(p[0] for p in points)))):
            clipped=points[:]
            for axis,limit,sign in ((0,x,1),(0,x+1,-1),(1,y,1),(1,y+1,-1)):
                output=[]
                for i,b in enumerate(clipped):
                    a=clipped[i-1];ina=(a[axis]-limit)*sign>=0;inb=(b[axis]-limit)*sign>=0
                    if ina!=inb:
                        t=(limit-a[axis])/(b[axis]-a[axis])
                        output.append(tuple(a[k]+t*(b[k]-a[k]) for k in (0,1)))
                    if inb:output.append(b)
                clipped=output
            if clipped:
                # Translate first to avoid cancellation at large page positions.
                ox,oy=clipped[0]
                area=abs(sum((a[0]-ox)*(b[1]-oy)-(a[1]-oy)*(b[0]-ox)
                             for a,b in zip(clipped,clipped[1:]+clipped[:1])))/2
                result[y*width+x]=min(255,math.floor(area*256))
    return bytes(result)


def open_line_polygon(tm,ctm,width,cap):
    a,b,c,d,tx,ty=tm;ca,cb,cc,cd=ctm
    points=[((ca*(a*x+tx)+cc*(b*x+ty)+1.125)*RATIO,
             (90-cb*(a*x+tx)-cd*(b*x+ty))*RATIO) for x in (0,12)]
    pen=(ca*96/72,-cb*96/72,cc*96/72,-cd*96/72)
    if width==0:pen=(1,0,0,1);width=1
    det=pen[0]*pen[3]-pen[1]*pen[2]
    dx,dy=(points[1][i]-points[0][i] for i in (0,1))
    ux=(pen[3]*dx-pen[2]*dy)/det;uy=(-pen[1]*dx+pen[0]*dy)/det
    length=math.hypot(ux,uy);ux/=length;uy/=length
    nx,ny=-uy*width/2,ux*width/2
    vx,vy=(ux*width/2,uy*width/2) if cap==2 else (0,0)
    transform=lambda x,y:(pen[0]*x+pen[2]*y,pen[1]*x+pen[3]*y)
    n=transform(nx,ny);v=transform(vx,vy)
    return [(points[0][0]+n[0]-v[0],points[0][1]+n[1]-v[1]),
            (points[1][0]+n[0]+v[0],points[1][1]+n[1]+v[1]),
            (points[1][0]-n[0]+v[0],points[1][1]-n[1]+v[1]),
            (points[0][0]-n[0]-v[0],points[0][1]-n[1]-v[1])]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe',type=Path)
    parser.add_argument('allocator',type=Path)
    parser.add_argument('cli',type=Path,nargs='?')
    args=parser.parse_args()
    probe,allocator=str(args.probe.resolve()),str(args.allocator.resolve())
    checks=allocations=references=0
    with tempfile.TemporaryDirectory(prefix='cupid-text-strokes-') as temporary:
        path=Path(temporary)/'case.pdf'
        def render(data,aa=1):
            nonlocal checks
            path.write_bytes(data)
            result=subprocess.run([probe,str(path),'0',str(aa)],capture_output=True,check=True,timeout=30)
            assert struct.unpack('<III',result.stdout[:12])==(WIDTH,HEIGHT,1)
            checks+=1
            return result.stdout[12:]
        def fault(data):
            nonlocal allocations
            path.write_bytes(data)
            result=subprocess.run([allocator,str(path),'0','1'],capture_output=True,text=True,check=True,timeout=120)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        for kind,size,width in itertools.product(MAKERS,(12,18,30),(.375,1.125,3.375,30)):
            actual=render(fixture(kind,text(size=size),width=width,stroke_color='1 0 0'))
            expected=red_stroke(stroke_rectangle(size,width))
            assert actual==expected,(kind,size,width,'analytic stroke area',sum(a!=b for a,b in zip(actual,expected)))
        stroke_mask=stroke_rectangle()
        box=[(0,0,1,0),(0,600,1,0),(400,600,1,0),(400,0,1,1)]
        for kind in MAKERS:
            fill_mask=top_down(rectangle_mask(transformed(box,30,TM),kind=='tt'))
            for ais,(fill,stroke),mask in itertools.product((False,True),((0,0),(.2,0),(.6,.4),(1,.4),(.6,1)),(None,.2,.6)):
                data=fixture(kind,text(2),fill=fill,stroke=stroke,ais=ais,mask=mask)
                actual=render(data)
                expected=combined(fill_mask,stroke_mask,fill,stroke,ais,1 if mask is None else mask)
                assert actual==expected,(kind,ais,fill,stroke,mask,'combined shape',sum(a!=b for a,b in zip(actual,expected)))
            for mode in (1,2):
                data=fixture(kind,text(mode),fill=.6,stroke=.4,ais=True,mask=.6)
                expected=render(data)
                assert render(fixture(kind,text(mode+4),fill=.6,stroke=.4,ais=True,mask=.6))==expected
                assert render(fixture(kind,text(mode),fill=.6,stroke=.4,ais=True,mask=.6,in_form=True))==expected
                assert render(fixture(kind,text(mode),fill=.6,stroke=.4,ais=True,mask=.6,pattern=True))==expected
                fault(data)
            # Stroke width is independent of font size and text horizontal scaling.
            left=render(fixture(kind,text(1,matrix=(2,0,0,1,TM[4],TM[5])),stroke_color='1 0 0'))
            right=render(fixture(kind,'BT /F 30 Tf 1 Tr 200 Tz '+f'{TM[4]} {TM[5]} Td (A) Tj ET',stroke_color='1 0 0'))
            assert left==right,(kind,'horizontal scale changed pen width')
        open_code=T1_WIDTH+command([0,0],21)+command([400,0],5)+b'\x0e'
        glyphs=[('.notdef',T1_WIDTH+b'\x0e'),('A',open_code)]
        # A small miter limit does not bound projecting square-cap extents.
        content='2 J 1 M '+text(1,matrix=(.6,.8,-.8,.6,TM[4],TM[5]))
        actual=render(fixture('type1',content,width=15,glyphs=glyphs),0)
        vector=f'2 J 1 M {TM[4]} {TM[5]} m {TM[4]+7.2} {TM[5]+9.6} l S'
        assert actual==render(fixture('type1',vector,width=15,glyphs=glyphs),0),'square cap clipped by glyph bounds'
        # Phase cycles and odd-array repetition retain the same user-space dash metric.
        for kind,mode in itertools.product(MAKERS,(1,2,5,6)):
            first=fixture(kind,'[3 2 1] .5 d '+text(mode),fill=.6,stroke=.4)
            second=fixture(kind,'[3 2 1 3 2 1] 12.5 d '+text(mode),fill=.6,stroke=.4)
            assert render(first)==render(second),(kind,mode,'dash phase cycle')
        for cap in (0,1,2):
            data=fixture('type1',f'{cap} J [0 4] 0 d '+text(),glyphs=glyphs)
            actual=render(data)
            if cap==0:
                assert actual==b'\xff'*len(actual)
            else:
                assert any(0<actual[i]<255 for i in range(0,len(actual),4))
            fault(data)
        matrices=((1,0,0,1,*TM[4:]),(.75,.25,-.125,1,40,30),(0,1,-1,0,60,30),(-1,0,0,1,60,30))
        ctms=((1,0,0,1),(1.5,0,.25,.75),(.7,.2,-.1,1.1))
        for cap,tm,ctm,width in itertools.product((0,2),matrices,ctms,(0,.375,1.125,8)):
            ca,cb,cc,cd=ctm
            expected=red_stroke(polygon_mask(open_line_polygon(tm,ctm,width,cap)))
            content=f'{ca} {cb} {cc} {cd} 0 0 cm {cap} J '+text(matrix=tm)
            actual=render(fixture('type1',content,width=width,glyphs=glyphs,stroke_color='1 0 0'))
            # Different floating-point operation order can straddle a coverage
            # quantization boundary. No pixel may differ by more than one level.
            assert max(abs(a-b) for a,b in zip(actual,expected))<=1,(cap,tm,ctm,width,'analytic cap polygon')
            references+=1
        if args.cli:
            from test_sixel import decode
            expected=render(fixture('type1',text(2),fill=.6,stroke=.4))
            result=subprocess.run([str(args.cli.resolve()),'--sixel','--sixel-dither','none','--pdf-text-aa',str(path),'160','120'],capture_output=True,check=True,timeout=30)
            transparent,dimensions,pixels=decode(result.stdout)
            assert not transparent and dimensions==(WIDTH,HEIGHT)
            assert pixels==[tuple(expected[i+c]*100//255 for c in range(3)) for i in range(0,len(expected),4)]
            checks+=1
    print(f'PDF text strokes: {checks} page checks; {allocations} allocation checks; '
          f'{references} independent grayscale stroke pages')


if __name__=='__main__':
    main()
