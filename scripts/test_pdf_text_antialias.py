#!/usr/bin/env python3
"""Qualify opt-in page glyph coverage with analytic areas and independent curves."""
import itertools
import json
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from test_pdf_embedded_fonts import fixture as tt_pdf
from test_pdf_cff_render import fixture as cff_pdf
from test_pdf_type1_render import fixture as type1_pdf

RATIO = struct.unpack('f', struct.pack('f', 96 / 72))[0]
WIDTH, HEIGHT = 160, 120
MAKERS = {'tt': tt_pdf, 'cff': cff_pdf, 'type1': type1_pdf}


def transformed(points, size, matrix):
    a,b,c,d,tx,ty = matrix
    # The test document prepends a 1.125-point horizontal translation.
    def q(value):
        return math.floor(value + .5)
    return [(q(((a*x+c*y)*size/1000 + tx+1.125)*RATIO*64),
             q((HEIGHT-(90-((b*x+d*y)*size/1000+ty))*RATIO)*64), tag, end)
            for x,y,tag,end in points]


def rgba_from_mask(mask, alpha=1):
    result = bytearray(b'\xff' * (WIDTH * HEIGHT * 4))
    for y in range(HEIGHT):
        for x in range(WIDTH):
            coverage = mask[(HEIGHT-1-y)*WIDTH+x]
            value = math.floor(255-alpha*coverage+.5)
            at = (y*WIDTH+x)*4
            result[at+1] = result[at+2] = value
    return bytes(result)


def rectangle_mask(points, clockwise):
    left, right = min(p[0] for p in points), max(p[0] for p in points)
    bottom, top = min(p[1] for p in points), max(p[1] for p in points)
    mask = bytearray(WIDTH*HEIGHT)
    for y in range(max(0,bottom//64), min(HEIGHT,(top+63)//64)):
        dy = max(0,min(top,(y+1)*64)-max(bottom,y*64))
        for x in range(max(0,left//64), min(WIDTH,(right+63)//64)):
            dx = max(0,min(right,(x+1)*64)-max(left,x*64))
            area = dx*dy
            coverage = area//16 if clockwise else ((area-1)//16 if area else 0)
            mask[y*WIDTH+x] = min(255,coverage)
    return mask


def main():
    probe, allocator = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
    checks = allocations = references = 0
    with tempfile.TemporaryDirectory(prefix='cupid-text-aa-') as directory:
        path = Path(directory)/'case.pdf'
        def render(data, option='1', status=0):
            nonlocal checks
            path.write_bytes(data)
            command = [probe,str(path),'0'] + ([] if option is None else [option])
            result = subprocess.run(command,capture_output=True,timeout=20)
            assert result.returncode == status, (result.returncode,result.stderr)
            checks += 1
            return result.stdout[12:]
        for kind,size,x,y,alpha in itertools.product(MAKERS,(12,18,30),(-5.125,20,20.1875),
                                                     (30,30.1875),(1,.5)):
            content = ('/Half gs ' if alpha==.5 else '')+f'1 0 0 rg BT /F {size} Tf {x} {y} Td (A) Tj ET'
            data = MAKERS[kind](content)
            points = [(0,0,1,0),(0,600,1,0),(400,600,1,0),(400,0,1,1)]
            expected = rgba_from_mask(rectangle_mask(transformed(points,size,(1,0,0,1,x,y)),
                                                     kind=='tt'),alpha)
            actual = render(data)
            assert actual == expected, (kind,size,x,y,alpha,sum(a!=b for a,b in zip(actual,expected)))
        # Explicit off and omitted options preserve the existing path.
        for kind in MAKERS:
            data=MAKERS[kind]('1 0 0 rg BT /F 30 Tf 20 30 Td (A) Tj ET')
            assert render(data,'0') == render(data,None)
            assert render(data,'1') != render(data,'0')
            render(data,'2',3)
            render(data,'-1',3)
            # Stroke-only, invisible and clip-only modes do not gain fill coverage.
            for mode in (1,3,5,7):
                data=MAKERS[kind](f'BT /F 30 Tf {mode} Tr 20 30 Td (A) Tj ET')
                assert render(data,'1') == render(data,'0')
            for mode in (0,2):
                a=MAKERS[kind](f'1 0 0 rg BT /F 30 Tf {mode} Tr 20 30 Td (A) Tj ET')
                b=MAKERS[kind](f'1 0 0 rg BT /F 30 Tf {mode+4} Tr 20 30 Td (A) Tj ET')
                assert render(a)==render(b),(kind,mode,'paint before deferred clip')
            for content in ('1 0 0 rg BT /F 30 Tf 20 30 Td (A) Tj ET',
                            '/Half gs 1 0 0 rg BT /F 30 Tf 20 30 Td (AA) Tj ET'):
                path.write_bytes(MAKERS[kind](content))
                result=subprocess.run([allocator,str(path),'0','1'],capture_output=True,text=True,timeout=90)
                assert result.returncode==0,(result.stdout,result.stderr)
                allocations+=int(result.stdout.split('checks')[0].split()[-1])
        # Inherited font resources in a Form must preserve the rendering option.
        from test_pdf_forms import form
        for kind in MAKERS:
            content='1 0 0 rg BT /F 18 Tf 20.1875 30.1875 Td (A) Tj ET'
            page=b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> /XObject << /G 20 0 R >> >> >>'
            data=MAKERS[kind]('/G Do',extra={3:page,20:form(content,'/BBox [0 0 120 90]')})
            assert render(data)==render(MAKERS[kind](content)), kind
        # Glyph coverage survives soft-mask and pattern composition exactly once.
        from test_pdf_type3 import stream
        for kind in MAKERS:
            glyph='BT /F 18 Tf 20.1875 30.1875 Td (A) Tj ET'
            expected=render(MAKERS[kind]('1 0 0 rg '+glyph))
            for subtype in ('Alpha','Luminosity'):
                page=('<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] '
                      '/Resources << /Font << /F 5 0 R >> /ExtGState << /M << /SMask '
                      '<< /S /'+subtype+' /G 20 0 R >> >> >> >> >>').encode()
                mask=form('1 1 1 rg '+glyph,'/BBox [0 0 120 90] /Group << /S /Transparency /I true /CS /DeviceRGB >>')
                data=MAKERS[kind]('/M gs 1 0 0 rg 0 0 120 90 re f',extra={3:page,20:mask})
                assert render(data)==expected,(kind,subtype)
            page=b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> /Pattern << /P 20 0 R >> >> >>'
            pattern=stream('1 0 0 rg 0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>')
            data=MAKERS[kind]('/Pattern cs /P scn '+glyph,extra={3:page,20:pattern})
            assert render(data)==expected,(kind,'pattern')
        if len(sys.argv) > 3:
            from test_sixel import decode
            binary=str(Path(sys.argv[3]).resolve())
            data=tt_pdf('1 0 0 rg BT /F 30 Tf 20 30 Td (A) Tj ET')
            expected=render(data)
            command=[binary,'--sixel','--sixel-dither','none','--pdf-page','1',
                     '--pdf-text-aa',str(path),'160','120']
            result=subprocess.run(command,capture_output=True,timeout=20)
            assert result.returncode==0,result.stderr
            transparent,dimensions,pixels=decode(result.stdout)
            assert not transparent and dimensions==(WIDTH,HEIGHT)
            want=[tuple(expected[i+c]*100//255 for c in range(3))
                  for i in range(0,len(expected),4)]
            assert pixels==want,'CLI smoothing/Sixel output differs from the page API'
            checks+=1
        try:
            from test_pdf_coverage_reference import reference_coverage
        except SystemExit:
            reference_coverage = None
        if reference_coverage:
            tt_points=json.loads(Path('tests/fixtures/pdf-truetype.json').read_text())['outlines'][3]['points']
            curves={
                'tt':(tt_points,'C'),
                'cff':([(0,0,1,0),(400,0,1,0),(400,600,2,0),(0,600,2,0),(0,0,1,1)],'B'),
                'type1':([(0,0,1,0),(400,0,1,0),(400,400,2,0),(0,400,2,0),(0,0,1,1)],'B')}
            matrices=((1,0,0,1,20,30),(.75,.25,-.125,1,40,30),(0,1,-1,0,60,30),(-1,0,0,1,60,30))
            for kind,size,matrix in itertools.product(MAKERS,(12,18,30),matrices):
                points,char=curves[kind]
                mask=reference_coverage(transformed(points,size,matrix),WIDTH,HEIGHT)
                kwargs = {'encoding': '<< /Differences [66 /customShape] >>'} if kind == 'cff' else {}
                data=MAKERS[kind](f'1 0 0 rg BT /F {size} Tf '+ ' '.join(map(str,matrix))+f' Tm ({char}) Tj ET', **kwargs)
                expected=rgba_from_mask(mask)
                actual=render(data)
                assert actual==expected,(kind,size,matrix,sum(a!=b for a,b in zip(actual,expected)))
                references+=1
    print(f'PDF text antialias: {checks} page checks; {allocations} allocation/cache checks; '
          f'{references} independent curve bitmaps; stroke and clip coverage remain binary')


if __name__=='__main__':
    main()
