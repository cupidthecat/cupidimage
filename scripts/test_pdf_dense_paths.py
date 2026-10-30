#!/usr/bin/env python3
"""Check bounded dense paths, paint identity and nested-state ownership."""
import itertools
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_forms import form
from test_pdf_type3 import stream


def page(content,objects=None,resources='',small=False):
    extra={5:'<< /ca .5 /CA .5 >>'};extra.update(objects or {})
    size='.75 .75' if small else '18 12'
    return document('/MediaBox [0 0 '+size+'] /Resources << /ExtGState << /Half 5 0 R >> '+resources+' >>',stream=content,extra=extra)


def rectangles(count):
    return '0 0 18 12 re '*count


def padded(count):
    return '0 0 m '+'0 0 l '*count+'18 0 l 18 12 l 0 12 l h '


def curves(count):
    return '0 0 m 0 1 1 1 1 0 c h '*count


def main():
    probe,allocator=[str(Path(x).resolve()) for x in sys.argv[1:3]]
    checks=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupid-dense-paths-') as directory:
        path=Path(directory)/'case.pdf'
        def render(data,reject=False,aa=False):
            nonlocal checks
            path.write_bytes(data)
            result=subprocess.run([probe,str(path),'0',*(['--pdf-text-aa','--pdf-graphics-aa'] if aa else [])],capture_output=True,timeout=60)
            assert (result.returncode!=0)==reject,(result.returncode,result.stderr)
            checks+=1
            if reject:return None
            w,h,n=struct.unpack('<III',result.stdout[:12]);assert n==1 and len(result.stdout)==12+w*h*4
            return result.stdout[12:]
        def fault(data,reject=False):
            nonlocal allocations
            path.write_bytes(data)
            result=subprocess.run([allocator,str(path),str(int(reject))],capture_output=True,text=True,check=True,timeout=120)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        for aa in (False,True):
            assert render(page(rectangles(1300)+'/Half gs 1 0 0 rg f'),aa=aa)==render(page(rectangles(1)+'/Half gs 1 0 0 rg f'),aa=aa)
            assert render(page(rectangles(1301)+'/Half gs 1 0 0 rg f*'),aa=aa)==render(page(rectangles(1)+'/Half gs 1 0 0 rg f*'),aa=aa)
            assert render(page(rectangles(1300)+'f*'),aa=aa)==bytes((255,255,255,255))*(24*16)
            for operation in ('f','S','B','b','W n 1 0 0 rg 0 0 18 12 re f'):
                prefix='/Half gs 1 0 0 rg 0 0 1 RG 1 w '
                assert render(page(prefix+padded(5000)+operation),aa=aa)==render(page(prefix+padded(0)+operation),aa=aa),(operation,aa)
            assert render(page(curves(2500)+'1 0 0 rg f'),aa=aa)==render(page(curves(1)+'1 0 0 rg f'),aa=aa)
        fault(page(rectangles(1300)+'/Half gs 1 0 0 rg f',small=True))
        fault(page(rectangles(5000)+'1 0 0 rg f',small=True))
        fault(page(curves(2500)+'1 0 0 rg f',small=True))
        child=form('0 0 1 rg 0 0 9 12 re f','/BBox [0 0 18 12]')
        tiled=stream('1 0 0 rg 0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>')
        shading='<< /PatternType 2 /Shading << /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Extend [true true] /Function << /FunctionType 2 /Domain [0 1] /C0 [0 1 0] /C1 [0 1 0] /N 1 >> >> >>'
        glyph='<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [.001 0 0 .001 0 0] /FirstChar 65 /LastChar 65 /Widths [1000] /Encoding << /Differences [65 /A] >> /CharProcs << /A 41 0 R >> >>'
        mask=form('1 g 0 0 9 12 re f','/BBox [0 0 18 12] /Group << /S /Transparency /CS /DeviceGray /I true >>')
        cases=[
            ('/G Do /Half gs 1 0 0 rg f',{20:child},'/XObject << /G 20 0 R >>'),
            ('/Half gs /Pattern cs /P scn f',{30:tiled},'/Pattern << /P 30 0 R >>'),
            ('/Half gs /Pattern cs /P scn f',{30:shading},'/Pattern << /P 30 0 R >>'),
            ('BT /F 9 Tf (A) Tj ET /Half gs 1 0 0 rg f',{40:glyph,41:stream('1000 0 d0 0 0 1 rg 0 0 1000 1000 re f')},'/Font << /F 40 0 R >>'),
            ('/Half gs 1 0 0 rg f',{5:'<< /SMask << /S /Alpha /G 20 0 R >> >>',20:mask},'')]
        for body,objects,resources in cases:
            large=page(rectangles(1300)+body,objects,resources)
            small=page(rectangles(1)+body,objects,resources)
            for aa in (False,True):assert render(large,aa=aa)==render(small,aa=aa)
            fault(page(rectangles(1300)+body,objects,resources,small=True))
        for suffix in ('','n'):
            data=page(rectangles(1300)+suffix)
            assert render(data)==bytes((255,255,255,255))*(24*16)
            fault(page(rectangles(1300)+suffix,small=True))
        bad=page(rectangles(1300)+'/Bad Do',{20:stream('','/Type /XObject /Subtype /Form')},'/XObject << /Bad 20 0 R >>',small=True)
        render(bad,True);fault(bad,True)
        render(page(rectangles(16385)+'f',small=True),True)
        fault(page(rectangles(16385)+'f',small=True),True)
        # Repeated independent small clips on a large canvas must not consume
        # a full page of geometric tests for every invocation.
        for shape in ('0 0 12 12 re', '0 0 m 12 0 l 0 12 l h'):
            def clipped(repeats):
                return document('/MediaBox [0 0 612 792]',
                    stream=('q '+shape+' W n Q ')*repeats+shape+' W n 0 g 0 0 12 12 re f')
            assert render(clipped(400),aa=True)==render(clipped(0),aa=True)
        # An empty clip still allocates and clears its bounded bitmap safely.
        data=page('40 40 1 1 re W n 0 0 18 12 re f',small=True)
        assert render(data,aa=True)==bytes((255,255,255,255))
        fault(data)
    print(f'PDF dense paths: {checks} page checks; {allocations} allocation checks')


if __name__=='__main__':main()
