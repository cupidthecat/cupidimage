#!/usr/bin/env python3
"""Check tiling-pattern phase, transparency, colour, resources and clipping."""
from pathlib import Path
import struct
import math
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm


def fixture(content='/Pattern cs /P scn 0 0 18 12 re f',program='1 0 0 rg 0 0 1.5 1.5 re f',
            paint=1,tiling=1,bbox='0 0 1.5 1.5',step='3',ystep='3',matrix='1 0 0 1 0 0',
            resources='<< >>',extra=None,pattern_extra='',page_extra=''):
    attrs=(f'/Type /Pattern /PatternType 1 /PaintType {paint} /TilingType {tiling} /BBox [{bbox}] '
           f'/XStep {step} /YStep {ystep} /Matrix [{matrix}] /Resources {resources} {pattern_extra}')
    objects={5:stream(program,attrs)};objects.update(extra or {})
    return document('/MediaBox [0 0 18 12] /Resources << /Pattern << /P 5 0 R >> '
                    '/ColorSpace << /PC [/Pattern /DeviceRGB] /PG [/Pattern /DeviceGray] /PK [/Pattern /DeviceCMYK] /PI [/Pattern [/Indexed /DeviceRGB 1 <ff000000ff00>]] >> '+page_extra+' >>',
                    stream=content,extra=objects)


def main():
    probe=str(Path(sys.argv[1]).resolve());count=refs=0
    red=(255,0,0);white=(255,255,255)
    base=lambda x,y:red if x%4<2 and y%4>=2 else white
    with tempfile.TemporaryDirectory(prefix='cupidimage-patterns-') as temp:
        def check(name,pdf,expected=None,reject=False,reference=True,secondary=False,tolerance=0,dimensions=(24,16)):
            nonlocal count,refs
            path=Path(temp)/'case.pdf';path.write_bytes(pdf)
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert run.returncode==(3 if reject else 0),(name,run.stderr)
            count+=1
            if reject:return
            assert struct.unpack('=III',run.stdout[:12])==(*dimensions,1)
            if expected:
                want=bytes(v for y in range(dimensions[1]) for x in range(dimensions[0]) for v in (*expected(x,y),255))
                assert run.stdout[12:]==want,(name,next((i,a,b) for i,(a,b) in enumerate(zip(run.stdout[12:],want)) if a!=b))
            if '--reference' in sys.argv and reference:
                if secondary:
                    import fitz
                    fitz.TOOLS.set_aa_level(0)
                    with fitz.open(path) as ref:
                        raster=ref[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                        w,h,rgb=raster.width,raster.height,raster.samples
                else:
                    prefix=Path(temp)/'reference'
                    ref=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=15)
                    assert ref.returncode==0,(name,ref.stderr)
                    w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                actual=bytes(v for i,v in enumerate(run.stdout[12:]) if i%4!=3)
                assert (w,h)==dimensions and max(abs(a-b) for a,b in zip(actual,rgb))<=tolerance,(name,'reference differs')
                refs+=1
        for tiling in (1,2,3):check('basic',fixture(tiling=tiling),base)
        check('cell-bbox',fixture(program='1 0 0 rg 0 0 3 3 re f'),base)
        check('negative-step',fixture(step='-3',ystep='-3'),base)
        check('phase',fixture(matrix='1 0 0 1 .75 .75'),lambda x,y:red if (x-1)%4<2 and (y+1)%4>=2 else white)
        check('scale',fixture(matrix='1.5 0 0 1 0 0'),lambda x,y:red if x%6<3 and y%4>=2 else white)
        check('reflection',fixture(matrix='-1 0 0 1 18 0'),lambda x,y:red if x%4>=2 and y%4>=2 else white)
        check('parent-ctm-does-not-shift-phase',fixture(content='1 0 0 1 .75 0 cm /Pattern cs /P scn -1 0 19 12 re f'),base)
        check('uncolored-rgb',fixture(content='/PC cs 1 0 0 /P scn 0 0 18 12 re f',paint=2,program='0 0 1.5 1.5 re f'),base)
        check('uncolored-gray',fixture(content='/PG cs .5 /P scn 0 0 18 12 re f',paint=2,program='0 0 1.5 1.5 re f'),
              lambda x,y:(128,128,128) if x%4<2 and y%4>=2 else white)
        check('uncolored-cmyk',fixture(content='/PK cs 0 1 1 0 /P scn 0 0 18 12 re f',paint=2,program='0 0 1.5 1.5 re f'),base,reference=False)
        check('uncolored-color-error',fixture(content='/PC cs 1 0 0 /P scn 0 0 18 12 re f',paint=2,program='0 1 0 rg 0 0 1.5 1.5 re f'),reject=True)
        check('plain-color-clears-pattern',fixture(content='/Pattern cs /P scn 0 0 18 12 re f 1 g 0 0 18 12 re f'),lambda x,y:white)
        check('q-Q-pattern',fixture(content='/Pattern cs /P scn q 1 g 0 0 18 12 re f Q 0 0 18 12 re f'),base)
        check('path-clip',fixture(content='0 0 9 12 re W n /Pattern cs /P scn 0 0 18 12 re f'),lambda x,y:base(x,y) if x<12 else white)
        mask=stream(b'00>','/Subtype /Image /Width 1 /Height 1 /ImageMask true /Filter /ASCIIHexDecode')
        check('image-mask',fixture(content='/Pattern cs /P scn 18 0 0 12 0 0 cm /Mask Do',extra={6:mask},page_extra='/XObject << /Mask 6 0 R >>'),base)
        check('compressed',fixture(program=zlib.compress(b'1 0 0 rg 0 0 1.5 1.5 re f'),pattern_extra='/Filter /FlateDecode'),base)
        check('empty-cell',fixture(program=''),lambda x,y:white)
        check('pattern-alpha',fixture(program='/Half gs 1 0 0 rg 0 0 1.5 1.5 re f',resources='<< /ExtGState << /Half << /ca .5 >> >> >>'),
              lambda x,y:(255,128,128) if x%4<2 and y%4>=2 else white,reference=False)
        check('uncolored-alpha',fixture(content='/PC cs 1 0 0 /P scn 0 0 18 12 re f',paint=2,
              program='/Half gs 0 0 1.5 1.5 re f',resources='<< /ExtGState << /Half << /ca .5 >> >> >>'),
              lambda x,y:(255,128,128) if x%4<2 and y%4>=2 else white,reference=False)
        check('blend-on-transparent-backdrop',fixture(program='/M gs 1 0 0 rg 0 0 1.5 1.5 re f',
              resources='<< /ExtGState << /M << /BM /Multiply >> >> >>'),base)
        check('pattern-stroke',fixture(content='/Pattern CS /P SCN 3 w 0 6 m 18 6 l S'),
              lambda x,y:base(x,y) if 6<=y<=9 else white)
        check('outer-alpha',fixture(content='/Half gs /Pattern cs /P scn 0 0 18 12 re f',
              page_extra='/ExtGState << /Half << /ca .5 >> >>'),
              lambda x,y:(255,128,128) if x%4<2 and y%4>=2 else white,reference=False)
        form=stream('/Pattern cs /P scn 0 0 18 12 re f',
              '/Type /XObject /Subtype /Form /BBox [0 0 18 12] /Resources << /Pattern << /P 5 0 R >> >>')
        check('form-alpha-reset',fixture(content='/Half gs /Form Do',extra={6:form},
              page_extra='/XObject << /Form 6 0 R >> /ExtGState << /Half << /ca .5 >> >>'),
              lambda x,y:(255,128,128) if x%4<2 and y%4>=2 else white,reference=False)
        shifted_form=stream('/Pattern cs /P scn 0 0 18 12 re f',
              '/Type /XObject /Subtype /Form /BBox [0 0 18 12] /Matrix [1 0 0 1 .75 0] /Resources << /Pattern << /P 5 0 R >> >>')
        check('form-origin',fixture(content='/Form Do',extra={6:shifted_form},page_extra='/XObject << /Form 6 0 R >>'),
              lambda x,y:red if (x-1)%4<2 and y%4>=2 else white)
        check('stroke-scale',document('/MediaBox [0 0 18 12]',stream='2 0 0 2 0 0 cm 1.5 w 0 3 m 9 3 l S'),
              lambda x,y:(0,0,0) if 6<=y<=9 else white)
        check('stroke-nonuniform',document('/MediaBox [0 0 18 12]',stream='2 0 0 1 0 0 cm 1.5 w 3 0 m 3 12 l S'),
              lambda x,y:(0,0,0) if 6<=x<=9 else white)
        check('stroke-rotation',document('/MediaBox [0 0 18 12]',stream='0 1 -1 0 18 0 cm 1.5 w 0 3 m 12 3 l S'),
              lambda x,y:(0,0,0) if 19<=x<=20 else white)
        check('stroke-shear',document('/MediaBox [0 0 18 12]',stream='1 0 1 1 0 0 cm 2.4 w 6 -100 m 6 100 l S'),
              lambda x,y:(0,0,0) if 22<=x+y<=24 else white,reference=False)
        check('hairline-scale',document('/MediaBox [0 0 18 12]',stream='1 0 0 3 0 0 cm 0 w 0 1.875 m 18 1.875 l S'),
              lambda x,y:(0,0,0) if y==8 else white,reference=False)
        check('wrapped-bbox',fixture(program='1 0 0 rg -1.5 0 3 1.5 re f',bbox='-1.5 0 1.5 1.5'),
              lambda x,y:red if y%4>=2 else white)
        check('overlapping-alpha-cells',fixture(program='/Half gs 1 0 0 rg 0 0 4.5 1.5 re f',bbox='0 0 4.5 1.5',
              resources='<< /ExtGState << /Half << /ca .5 >> >> >>'),
              lambda x,y:((255,64,64) if x%4<2 else (255,128,128)) if y%4>=2 else white,reference=False)
        inner=stream('1 0 0 rg 0 0 1.5 1.5 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 1.5 1.5] /XStep 3 /YStep 3 /Resources << >>')
        check('nested-pattern',fixture(program='/Pattern cs /Q scn 0 0 3 3 re f',bbox='0 0 3 3',
              resources='<< /Pattern << /Q 6 0 R >> >>',extra={6:inner}),base,secondary=True)
        check('outer-blend',fixture(content='0 0 1 rg 0 0 18 12 re f /M gs /Pattern cs /P scn 0 0 18 12 re f',
              page_extra='/ExtGState << /M << /BM /Multiply >> >>'),
              lambda x,y:(0,0,0) if x%4<2 and y%4>=2 else (0,0,255))
        check('fractional-no-distortion',fixture(tiling=2,step='2.5',bbox='0 0 1.25 1.5',program='1 0 0 rg 0 0 1.25 1.5 re f'),
              lambda x,y:red if ((x+.5)*.75)%2.5<1.25 and y%4>=2 else white,reference=False)
        font=('<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1.5 1.5] /FontMatrix [1 0 0 1 0 0] '
              '/Encoding << /Differences [65 /A] >> /FirstChar 65 /LastChar 65 /Widths [1.5] /CharProcs << /A 7 0 R >> >>')
        check('type3-cell',fixture(program='BT /F 1 Tf (A) Tj ET',resources='<< /Font << /F 6 0 R >> >>',
              extra={6:font,7:stream('1.5 0 d0 1 0 0 rg 0 0 1.5 1.5 re f')}),base)
        check('rotation',fixture(matrix='0 1 -1 0 18 0',bbox='0 0 1.5 .75',program='1 0 0 rg 0 0 1.5 .75 re f'),
              lambda x,y:red if x%4==3 and y%4>=2 else white)
        check('shear',fixture(tiling=2,matrix='1 0 .5 1 0 0'),
              lambda x,y:red if (((x+.5)*.75-.5*(12-(y+.5)*.75))%3)<1.5 and y%4>=2 else white,reference=False)
        check('empty-bbox',fixture(bbox='0 0 0 1000000000'),lambda x,y:white,reference=False)
        check('indexed-uncolored',fixture(content='/PI cs 1 /P scn 0 0 18 12 re f',paint=2,program='0 0 1.5 1.5 re f',
              page_extra='/Ignored true'),lambda x,y:(0,255,0) if x%4<2 and y%4>=2 else white)
        def shading_pattern(shading,content='/Pattern cs /P scn 0 0 18 12 re f',pattern_attrs='',page_extra=''):
            objects={5:'<< /PatternType 2 /Shading 6 0 R '+pattern_attrs+' >>',6:shading}
            return document('/MediaBox [0 0 18 12] /Resources << /Pattern << /P 5 0 R >> '+page_extra+' >>',stream=content,extra=objects)
        axial=('<< /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 18 0] '
               '/Function << /FunctionType 2 /Domain [0 1] /N 1 >> >>')
        gray=lambda x,y:(round((x+.5)/24*255),)*3
        check('axial-shading-pattern',shading_pattern(axial),gray,tolerance=1)
        check('shading-pattern-mask',shading_pattern(axial,content='/Pattern cs /P scn 0 0 9 12 re f'),
              lambda x,y:gray(x,y) if x<12 else white,tolerance=1)
        check('shading-pattern-ctm',shading_pattern(axial,content='1 0 0 1 .75 0 cm /Pattern cs /P scn -1 0 19 12 re f'),gray,tolerance=1)
        check('shading-pattern-matrix',shading_pattern(axial,pattern_attrs='/Matrix [1 0 0 1 3 0]'),
              lambda x,y:(round((x+.5-4)/24*255),)*3 if x>=4 else white,tolerance=1)
        check('shading-pattern-alpha',shading_pattern(axial,content='/Half gs /Pattern cs /P scn 0 0 18 12 re f',
              page_extra='/ExtGState << /Half << /ca .5 >> >>'),
              lambda x,y:(int((x+.5)/24*255*.5+127.5+.5),)*3,reference=False)
        background=('<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [3 0 15 0] /Background [0 1 0] '
                    '/Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >> >>')
        background_pixel=lambda x,y:(round((1-(x+.5-4)/16)*255),0,round((x+.5-4)/16*255)) if 4<=x<20 else (0,255,0)
        # A finite axial endpoint has a one-column coverage difference in the
        # command-line reference; the analytic pixel-centre check remains exact.
        check('shading-background',shading_pattern(background),background_pixel,reference=False)
        full_background=background.replace('[3 0 15 0]','[6 0 18 0]').replace('/Background','/BBox [3 0 15 12] /Background')
        full_color=lambda x,y:(round((1-(x+.5-8)/16)*255),0,round((x+.5-8)/16*255))
        check('shading-background-bbox',shading_pattern(full_background),
              lambda x,y:full_color(x,y) if 8<=x<20 else (0,255,0) if 4<=x<8 else white,tolerance=1)
        check('default-shading-extstate',shading_pattern(axial,pattern_attrs='/ExtGState << /ca 1 /BM /Normal /SMask /None >>'),gray,tolerance=1)
        check('shading-extstate-alpha',shading_pattern(axial,pattern_attrs='/ExtGState << /ca .5 >>'),
              lambda x,y:(int(((x+.5)/24*.5+.5)*255+.5),)*3,reference=False)
        check('shading-extstate-blend',shading_pattern(axial,pattern_attrs='/ExtGState << /BM /Multiply >>'),gray,tolerance=1)
        check('singular-shading-pattern',shading_pattern(axial,pattern_attrs='/Matrix [0 0 0 0 0 0]'),reject=True)
        # The sh operator uses its own colours, even when a pattern is selected.
        direct_objects={6:axial}
        check('sh-ignores-selected-pattern',fixture(content='/Pattern cs /P scn /Shade sh',extra=direct_objects,
              page_extra='/Shading << /Shade 6 0 R >>'),gray,tolerance=1)
        radial=('<< /ShadingType 3 /ColorSpace /DeviceGray /Coords [9 6 0 9 6 6] '
                '/Function << /FunctionType 2 /Domain [0 1] /N 1 >> >>')
        def radial_pixel(x,y):
            t=math.hypot((x+.5)*.75-9,12-(y+.5)*.75-6)/6
            return (round(t*255),)*3 if t<=1 else (0,0,255)
        check('radial-shading-pattern',shading_pattern(radial,content='0 0 1 rg 0 0 18 12 re f /Pattern cs /P scn 0 0 18 12 re f'),
              radial_pixel,reference=False)
        large_radial=radial.replace('[9 6 0 9 6 6]','[225 150 0 225 150 150]').replace('/Function','/Extend [true true] /Function')
        large_doc=document('/MediaBox [0 0 450 300] /Resources << /Pattern << /P 5 0 R >> >>',
              stream='/Pattern cs /P scn 0 0 450 300 re f',extra={5:'<< /PatternType 2 /Shading 6 0 R >>',6:large_radial})
        # Larger native-resolution comparisons bound nonlinear reference
        # approximation; the small transparent-domain case stays analytic.
        check('radial-pattern-reference',large_doc,
              lambda x,y:(round(min(1,math.hypot((x+.5)*.75-225,300-(y+.5)*.75-150)/150)*255),)*3,
              dimensions=(600,400),tolerance=2)
        function=stream('{0}','/FunctionType 4 /Domain [0 1 0 1] /Range [0 1 0 1 0 1]')
        function_shading='<< /ShadingType 1 /ColorSpace /DeviceRGB /Matrix [18 0 0 12 0 0] /Function 7 0 R >>'
        function_doc=document('/MediaBox [0 0 18 12] /Resources << /Pattern << /P 5 0 R >> >>',
              stream='/Pattern cs /P scn 0 0 18 12 re f',extra={5:'<< /PatternType 2 /Shading 6 0 R >>',6:function_shading,7:function})
        check('function-shading-pattern',function_doc,lambda x,y:(round((x+.5)/24*255),round((15.5-y)/16*255),0),reference=False)
        large_function=document('/MediaBox [0 0 450 300] /Resources << /Pattern << /P 5 0 R >> >>',
              stream='/Pattern cs '+('/P scn '*128)+'0 0 450 300 re f',
              extra={5:'<< /PatternType 2 /Shading 6 0 R >>',6:function_shading.replace('[18 0 0 12 0 0]','[450 0 0 300 0 0]'),7:function})
        check('function-pattern-cache-reference',large_function,
              lambda x,y:(round((x+.5)/600*255),round((399.5-y)/400*255),0),dimensions=(600,400),tolerance=2)
        check('direct-shading-ignores-background',fixture(content='/Pattern cs /P scn /Shade sh',extra={6:full_background},
              page_extra='/Shading << /Shade 6 0 R >>'),
              lambda x,y:full_color(x,y) if 8<=x<20 else white,tolerance=1)
        check('shading-preserves-pattern-color',fixture(content='/Pattern cs /P scn /Shade sh 0 0 9 12 re f',extra={6:axial},
              page_extra='/Shading << /Shade 6 0 R >>'),
              lambda x,y:base(x,y) if x<12 and base(x,y)==red else gray(x,y),tolerance=1)
        for kwargs in [dict(step='0'),dict(ystep='0'),dict(paint=3),dict(tiling=0),dict(tiling=4),
                       dict(matrix='0 0 0 0 0 0'),dict(matrix='0 0 0 1 0 0'),dict(matrix='1 0 0 0 0 0'),dict(matrix='1 1 2 2 0 0'),dict(bbox='2 0 1 1'),dict(step='1000000000'),
                       dict(resources='17'),dict(paint=2),dict(content='/PC cs 1 0 0 /P scn 0 0 18 12 re f'),
                       dict(content='/Pattern cs /P sc 0 0 18 12 re f')]:
            check('invalid',fixture(**kwargs),reject=True)
        check('initial-empty-pattern',fixture(content='/Pattern cs 0 0 18 12 re f'),lambda x,y:white)
        recursive=stream('/Pattern cs /P scn 0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << /Pattern << /P 5 0 R >> >>')
        check('recursive',fixture(extra={5:recursive}),reject=True)
    print(f'PDF patterns: {count} checks passed; {refs} independent comparisons')


if __name__=='__main__':main()
