#!/usr/bin/env python3
"""Check geometric clip intersections and clipping once across group boundaries."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
from test_pdf_pages import document
from test_pdf_forms import form
from test_pdf_text_antialias import MAKERS


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe',type=Path)
    parser.add_argument('allocator',type=Path)
    args=parser.parse_args()
    probe,allocator=str(args.probe.resolve()),str(args.allocator.resolve())
    checks=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupid-fractional-clips-') as tmp:
        path=Path(tmp)/'case.pdf'
        def render(data,aa=1):
            nonlocal checks
            path.write_bytes(data)
            result=subprocess.run([probe,str(path),'0',str(aa)],capture_output=True,check=True,timeout=30)
            w,h,n=struct.unpack('<III',result.stdout[:12]);assert n==1
            checks+=1
            return w,h,result.stdout[12:]
        def fault(data):
            nonlocal allocations
            path.write_bytes(data)
            result=subprocess.run([allocator,str(path),'0','1'],capture_output=True,text=True,check=True,timeout=120)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        def tiny(content,objects=None):
            return document('/MediaBox [0 0 3 3] /Resources << /XObject << /G 10 0 R >> >>',stream=content,extra=objects)
        fill='1 0 0 rg 0 0 3 3 re f'
        clip='0 0 .1875 3 re W n '
        expected=bytes(v for y in range(4) for x in range(4) for v in (255,191 if x==0 else 255,191 if x==0 else 255,255))
        assert render(tiny(clip+fill))==(4,4,expected)
        for count in (2,5,20,100):
            assert render(tiny(clip*count+fill))==(4,4,expected),'repeated clipping multiplied coverage'
        assert render(tiny(clip+'.375 0 .1875 3 re W n '+fill))[2]==b'\xff'*64,'disjoint subpixel clips leaked'
        assert render(tiny('q '+clip+fill+' Q'))[2]==expected
        assert render(tiny('q '+clip+'Q '+fill))[2]==bytes((255,0,0,255))*16
        assert render(tiny(clip+fill),0)[2]==b'\xff'*64,'binary compatibility changed'
        for group in ('','/Group << /S /Transparency /I true /CS /DeviceRGB >>',
                      '/Group << /S /Transparency /I false /CS /DeviceRGB >>'):
            objects={10:form(fill,'/BBox [0 0 3 3] '+group)}
            data=tiny(clip+'/G Do',objects)
            assert render(data)[2]==expected,'parent clip applied twice to Form group'
            fault(data)
            objects={10:form(fill,'/BBox [0 0 .1875 3] '+group)}
            data=tiny('/G Do',objects)
            assert render(data)[2]==expected,'fractional Form boundary'
            fault(data)
        # BBox and the inherited clip intersect geometrically in the same pixel.
        objects={10:form(fill,'/BBox [.375 0 .5625 3]')}
        assert render(tiny(clip+'/G Do',objects))[2]==b'\xff'*64
        text='BT /F 30 Tf 7 Tr 20.1875 30.1875 Td (A) Tj ET '
        paint='1 0 0 rg 0 0 120 90 re f'
        for kind,maker in MAKERS.items():
            data=maker(text+paint);_,_,base=render(data)
            assert any(0<v<255 for v in base),'text clip has no fractional coverage'
            resources={3:b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> /ExtGState << /Invisible << /ca 0 /CA 0 >> /Visible << /ca 1 /CA 1 >> >> >> >>'}
            for mode in (4,5,6,7):
                content='/Invisible gs '+text.replace('7 Tr',f'{mode} Tr')+'/Visible gs '+paint
                assert render(maker(content,extra=resources))[2]==base,(kind,mode,'deferred clipping mode')
            assert render(maker(text+text+paint))[2]==base,(kind,'repeated text clip')
            assert render(maker('q '+text+paint+' Q'))[2]==base,(kind,'saved text clip')
            assert render(maker('q '+text+' Q '+paint))[2]==render(maker(paint))[2]
            fault(data)
            tiny_clip='BT /F .46875 Tf 7 Tr 0 0 Td (A) Tj ET '
            tiny_paint='1 0 0 rg BT /F .46875 Tf 0 Tr 0 0 Td (A) Tj ET'
            assert render(maker(tiny_clip+tiny_paint))[2]==render(maker(tiny_paint))[2],(kind,'self clip squared glyph coverage')
            fault(maker(tiny_clip+tiny_paint))
            fault(maker(tiny_clip+'.09375 w 1 j BT /F .46875 Tf 2 Tr 0 0 Td (A) Tj ET'))
            assert render(maker(tiny_clip+'BT /F .46875 Tf 0 Tr .1875 0 Td (A) Tj ET'))[2]==b'\xff'*(160*120*4),(kind,'disjoint glyphs in one pixel')
            assert render(maker(tiny_clip+'.09375 w 1 j BT /F .46875 Tf 1 Tr .28125 0 Td (A) Tj ET'))[2]==b'\xff'*(160*120*4),(kind,'disjoint clipped stroke')
            ring='20 30 .75 .75 re 20.1875 30.1875 .375 .375 re W* n '
            assert render(maker(ring+'BT /F .46875 Tf 0 Tr 20.1875 30.1875 Td (A) Tj ET'))[2]==b'\xff'*(160*120*4),(kind,'glyph inside fractional clip hole')
        from test_pdf_empty_glyphs import fixture as empty_fixture
        for kind in ('tt','cff','type1','bitmap','standard','substitute'):
            assert render(empty_fixture('BT /F 30 Tf 7 Tr ( ) Tj ET '+paint,kind=kind))[2]==render(empty_fixture(paint,kind=kind))[2],(kind,'empty glyph changed clip')
            assert render(empty_fixture('BT /F 0 Tf 7 Tr (A) Tj ET '+paint,kind=kind))[2]==b'\xff'*(160*120*4),(kind,'collapsed nonempty outline did not clip')
        # A dense clip is equivalent to painting its disjoint rows separately.
        # Keep the normal page budget: increasing it would hide event duplication.
        from test_pdf_embedded_fonts import fixture as tt_fixture
        page={3:b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F 5 0 R >> >> >>'}
        blue='0 0 1 rg 0 0 612 792 re f'
        for rows in (10,30):
            lines=[f'1 0 0 1 6 {750-row*20} Tm ('+'C'*50+') Tj' for row in range(rows)]
            together='BT /F 12 Tf 7 Tr '+' '.join(lines)+' ET '+blue
            separate=' '.join('q BT /F 12 Tf 7 Tr '+line+' ET '+blue+' Q' for line in lines)
            whole=render(tt_fixture(together,extra=page))
            assert any(v!=255 for v in whole[2]),'dense clip produced a blank page'
            assert whole==render(tt_fixture(separate,extra=page)),(rows,'dense clip partition')
        fault(tt_fixture('BT /F 12 Tf 7 Tr '+lines[0]+' ET '+blue,extra=page))
    print(f'PDF fractional clips: {checks} page checks; {allocations} allocation checks')


if __name__=='__main__':
    main()
