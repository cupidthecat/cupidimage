#!/usr/bin/env python3
"""Check Identity CID metrics independently of fallback glyph outlines."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_type3 import stream


def fixture(content,metrics='',vertical=False,extra=None,descendant=None):
    objects={5:('<< /Type /Font /Subtype /Type0 /Encoding /Identity-'+('V' if vertical else 'H')+
                ' /DescendantFonts [6 0 R] /ToUnicode 7 0 R >>').encode(),
             6:('<< /Type /Font /Subtype /CIDFontType2 /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> '+metrics+' >>').encode(),
             7:stream('4 beginbfchar <0001> <0041> <0002> <0042> <0020> <0041> <FFFF> <0042> endbfchar')}
    if descendant:objects[6]=descendant
    objects.update(extra or {})
    return document('/MediaBox [0 0 120 90] /Resources << /Font << /CID 5 0 R /Simple << /Type /Font >> >> >>',stream=content,extra=objects)


def main():
    probe=str(Path(sys.argv[1]).resolve());count=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-cid-') as tmp:
        path=Path(tmp)/'case.pdf'
        def render(content,**kwargs):
            path.write_bytes(fixture(content,**kwargs));return subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
        def same(name,content,expected,**kwargs):
            nonlocal count
            a=render(content,**kwargs);b=render(expected,**kwargs)
            assert a.returncode==b.returncode==0,(name,a.stderr,b.stderr)
            assert a.stdout==b.stdout,(name,'pixels differ')
            assert any(v!=255 for v in a.stdout[12:]),name
            count+=1
        def expected_horizontal(advance,size=12,scale=100):
            return f'BT /Simple {size} Tf {scale} Tz 20 40 Td (A) Tj 1 0 0 1 {20+advance} 40 Tm (B) Tj ET'
        for width in (0,250,500,1000,-100):
            for metrics in (f'/DW {width}',f'/W [1 [{width} 700]]',f'/W [1 2 {width}]'):
                same('horizontal-width','BT /CID 12 Tf 20 40 Td <00010002> Tj ET',expected_horizontal(width*.012),metrics=metrics)
        for scale in (50,150,-100):
            same('scaled-CID-width',f'BT /CID 12 Tf {scale} Tz 20 40 Td <00010002> Tj ET',expected_horizontal(6*scale/100,scale=scale),metrics='/W [1 [500]]')
        same('default-width','BT /CID 12 Tf 20 40 Td <00010002> Tj ET',expected_horizontal(12))
        same('character-spacing','BT /CID 12 Tf 3 Tc 20 40 Td <00010002> Tj ET',expected_horizontal(15))
        same('no-two-byte-word-spacing','BT /CID 12 Tf 30 Tw 20 40 Td <00200002> Tj ET',expected_horizontal(12))
        same('horizontal-TJ','BT /CID 12 Tf 20 40 Td [<0001> 250 <0002>] TJ ET',expected_horizontal(9))
        same('max-CID','BT /CID 12 Tf 20 40 Td <0001FFFF> Tj ET',expected_horizontal(12),metrics='/W [65535 [500]]')
        same('indirect-metrics','BT /CID 12 Tf 20 40 Td <00010002> Tj ET',expected_horizontal(6),
             metrics='/DW 8 0 R /W 9 0 R',extra={8:b'1000',9:b'[1 10 0 R]',10:b'[11 0 R 700]',11:b'500'})
        for metrics,advance,vx,vy in (('',-1,.5,.88),('/DW 600',-1,.3,.88),('/DW2 [900 -1200]',-1.2,.5,.9),
                                      ('/W2 [1 [-800 200 700 -800 200 700]]',-.8,.2,.7),
                                      ('/W2 [1 2 -800 200 700]',-.8,.2,.7)):
            for scale in (100,50):
                x=60-vx*12*scale/100;y=70-vy*12
                expected=f'BT /Simple 12 Tf {scale} Tz {x} {y} Td (A) Tj 1 0 0 1 {x} {y+advance*12} Tm (B) Tj ET'
                same('vertical-origin-and-advance',f'BT /CID 12 Tf {scale} Tz 60 70 Td <00010002> Tj ET',expected,metrics=metrics,vertical=True)
        same('vertical-TJ','BT /CID 12 Tf 50 Tz 60 70 Td [<0001> 250 <0002>] TJ ET',
             'BT /Simple 12 Tf 50 Tz 57 59.44 Td (A) Tj 1 0 0 1 57 44.44 Tm (B) Tj ET',vertical=True)
        same('vertical-character-spacing','BT /CID 12 Tf 3 Tc 60 70 Td <00010002> Tj ET',
             'BT /Simple 12 Tf 54 59.44 Td (A) Tj 1 0 0 1 54 50.44 Tm (B) Tj ET',vertical=True)
        same('vertical-indirect','BT /CID 12 Tf 60 70 Td <00010002> Tj ET',
             'BT /Simple 12 Tf 57.6 61.6 Td (A) Tj 1 0 0 1 57.6 52 Tm (B) Tj ET',vertical=True,
             metrics='/DW2 8 0 R /W2 9 0 R',extra={8:b'[880 -1000]',9:b'[1 10 0 R]',10:b'[-800 200 700 -800 200 700]'})
        same('indirect-encoding','BT /CID 12 Tf 20 40 Td <00010002> Tj ET',expected_horizontal(6),metrics='/DW 500',
             extra={5:b'<< /Subtype /Type0 /Encoding 8 0 R /DescendantFonts [6 0 R] /ToUnicode 7 0 R >>',8:b'/Identity-H'})
        same('escaped-encoding','BT /CID 12 Tf 20 40 Td <00010002> Tj ET',expected_horizontal(6),metrics='/DW 500',
             extra={5:b'<< /Subtype /Type0 /Encoding /Identity#2dH /DescendantFonts [6 0 R] /ToUnicode 7 0 R >>'})
        same('indirect-descendant-array','BT /CID 12 Tf 20 40 Td <00010002> Tj ET',expected_horizontal(6),metrics='/DW 500',
             extra={5:b'<< /Subtype /Type0 /Encoding /Identity-H /DescendantFonts 8 0 R /ToUnicode 7 0 R >>',8:b'[6 0 R]'})
        same('missing-Unicode-is-not-CID-as-Unicode','BT /CID 12 Tf 20 40 Td <00410042> Tj ET',
             'BT /Simple 12 Tf 20 40 Td (?) Tj 1 0 0 1 32 40 Tm (?) Tj ET')
        same('q-Q-writing-mode','BT /CID 12 Tf 60 70 Td q /Simple 12 Tf Q <00010002> Tj ET',
             'BT /Simple 12 Tf 54 59.44 Td (A) Tj 1 0 0 1 54 47.44 Tm (B) Tj ET',vertical=True)
        for a,b,c,d in ((1,0,0,1),(0,1,-1,0),(1,.25,.1,1),(-1,0,0,1),(1,0,0,-1)):
            for size in (12,-12):
                dx=-.2*size;dy=-.7*size
                x=60+a*dx+c*dy;y=45+b*dx+d*dy
                x2=x+c*(-.8*size);y2=y+d*(-.8*size)
                expected=f'BT /Simple {size} Tf {a} {b} {c} {d} {x} {y} Tm (A) Tj {a} {b} {c} {d} {x2} {y2} Tm (B) Tj ET'
                same('vertical-affine',f'BT /CID {size} Tf {a} {b} {c} {d} 60 45 Tm <00010002> Tj ET',expected,
                     metrics='/W2 [1 2 -800 200 700]',vertical=True)
        for mode in (0,1,2,3,4,5,6,7):
            fill='0 0 120 90 re f' if mode>=4 else '0 0 1 1 re f'
            same('vertical-paint-mode',f'BT /CID 12 Tf {mode} Tr 60 70 Td <00010002> Tj ET {fill}',
                 f'BT /Simple 12 Tf {mode} Tr 54 59.44 Td (A) Tj 1 0 0 1 54 47.44 Tm (B) Tj ET {fill}',vertical=True)
        for vertical in (False,True):
            same('incomplete-code-substitution','BT /CID 12 Tf 60 70 Td <000100> Tj ET',
                 'BT /CID 12 Tf 60 70 Td <00010000> Tj ET',vertical=vertical)
    print(f'PDF CID metrics: {count} checks passed')
if __name__=='__main__':main()
