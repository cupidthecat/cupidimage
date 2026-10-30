#!/usr/bin/env python3
"""Check stroke geometry and state against analytic shapes and optional rasters."""
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from audit_pdf_corpus import read_ppm


def fixture(content, resources='', extra=None):
    return document('/MediaBox [0 0 36 30] /Resources << '+resources+' >>',stream=content,extra=extra)


def main():
    probe=str(Path(sys.argv[1]).resolve());count=refs=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-strokes-') as tmp:
        path=Path(tmp)/'case.pdf'
        def render(content,resources='',reject=False,extra=None):
            path.write_bytes(fixture(content,resources,extra))
            result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert result.returncode==(3 if reject else 0),(content,result.stderr)
            if reject:return None
            assert struct.unpack('=III',result.stdout[:12])==(48,40,1)
            return result.stdout[12:]
        def check(name,content,equivalent=None,expected=None,reject=False,resources='',reference=True,secondary=False,extra=None):
            nonlocal count,refs
            actual=render(content,resources,reject,extra);count+=1
            if reject:return
            if equivalent is not None:
                want=render(equivalent,resources,extra=extra)
                assert actual==want,(name,'analytic shape differs',next((i,a,b) for i,(a,b) in enumerate(zip(actual,want)) if a!=b))
            if expected:
                want=bytes(v for y in range(40) for x in range(48) for v in ((0,0,0,255) if expected((x+.5)*.75,30-(y+.5)*.75) else (255,255,255,255)))
                assert actual==want,(name,'analytic pixels differ',next((i,a,b) for i,(a,b) in enumerate(zip(actual,want)) if a!=b))
            if '--reference' in sys.argv and reference:
                path.write_bytes(fixture(content,resources,extra));prefix=Path(tmp)/'reference'
                if secondary:
                    import fitz
                    fitz.TOOLS.set_aa_level(0)
                    with fitz.open(path) as pdf:
                        raster=pdf[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                        w,h,rgb=raster.width,raster.height,raster.samples
                else:
                    result=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=15)
                    assert result.returncode==0,result.stderr
                    w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                assert (w,h)==(48,40)
                got=bytes(v for i,v in enumerate(actual) if i%4!=3)
                assert got==rgb,(name,'independent raster differs',sum(a!=b for a,b in zip(got,rgb)))
                refs+=1
        for cap in range(3):
            check('cap-'+str(cap),f'{cap} J 6 w 9 15 m 27 15 l S',
                  expected=lambda x,y,c=cap: (9<=x<=27 and abs(y-15)<=3) or
                    (c==1 and min((x-9)**2+(y-15)**2,(x-27)**2+(y-15)**2)<=9) or
                    (c==2 and 6<=x<=30 and abs(y-15)<=3),reference=cap!=1)
        check('default-butt','6 w 9 15 m 27 15 l S',equivalent='9 12 18 6 re f')
        for join in range(3):
            shape='6 6 m 12 6 l 12 18 l 30 18 l 30 24 l 9 24 l 9 21 l 6 21 l h f'
            if join==0:shape='6 6 m 12 6 l 12 18 l 30 18 l 30 24 l 6 24 l h f'
            elif join==2:shape='6 6 m 12 6 l 12 18 l 30 18 l 30 24 l 9 24 l 6 21 l h f'
            check('join-'+str(join),f'{join} j 6 w 9 6 m 9 21 l 30 21 l S',equivalent=shape if join!=1 else None,reference=join==0,secondary=True)
        check('miter-limit','1 M 6 w 9 6 m 9 21 l 30 21 l S',equivalent='2 j 6 w 9 6 m 9 21 l 30 21 l S',reference=False)
        check('closed-miter','6 w 9 9 18 12 re S',equivalent='6 6 24 18 re 12 12 12 6 re f*')
        check('closed-square-cap-ignored','2 J 6 w 9 9 18 12 re S',equivalent='6 w 9 9 18 12 re S')
        check('round-dot','1 J 6 w 18 15 m 18 15 l S',expected=lambda x,y:(x-18)**2+(y-15)**2<=9,reference=False)
        check('butt-zero','0 J 6 w 18 15 m 18 15 l S',expected=lambda x,y:False)
        check('square-zero','2 J 6 w 18 15 m 18 15 l S',expected=lambda x,y:False,reference=False)
        for dash,phase in (('6 3',0),('6 3',2),('6',0),('6 3 3',0),('6 3',20)):
            vals=[float(v) for v in dash.split()];vals=vals*2 if len(vals)%2 else vals
            def on(x,y):
                if not (6<=x<=30 and abs(y-15)<=1.5):return False
                at=(x-6+phase)%sum(vals)
                for i,v in enumerate(vals):
                    if at<v:return i%2==0
                    at-=v
                return False
            check('dash-'+dash+'-'+str(phase),f'[{dash}] {phase} d 3 w 6 15 m 30 15 l S',expected=on,reference=phase!=2)
        check('dash-reset-subpaths','[6 3] 0 d 3 w 6 9 m 30 9 l 6 21 m 30 21 l S',equivalent='[6 3] 0 d 3 w 6 9 m 30 9 l S 6 21 m 30 21 l S')
        check('q-Q','[6 3] 0 d 2 J 3 w q [] 0 d 0 J 9 w Q 6 15 m 30 15 l S',equivalent='[6 3] 0 d 2 J 3 w 6 15 m 30 15 l S')
        check('dash-through-corner','[30 3] 0 d 6 w 9 6 m 9 21 l 24 21 l S',equivalent='6 w 9 6 m 9 21 l 24 21 l S',secondary=True)
        check('scaled-width','2 0 0 1 0 0 cm 3 w 6 6 m 6 24 l S',equivalent='9 6 6 18 re f',secondary=True)
        check('scaled-dash','2 0 0 1 0 0 cm [3 1.5] 0 d 3 w 3 15 m 15 15 l S',equivalent='[6 3] 0 d 3 w 6 15 m 30 15 l S')
        check('alpha-no-overpaint','/Half gs 1 J 1 j 6 w 9 6 m 9 21 l 30 21 l S',resources='/ExtGState << /Half << /CA .5 >> >>',reference=False)
        check('graphics-state-dictionary','/Style gs 6 15 m 30 15 l S',
              resources='/ExtGState << /Style << /LC 2 /LJ 2 /ML 2 /LW 3 /D [[6 3] 0] >> >>',
              equivalent='2 J 2 j 2 M 3 w [6 3] 0 d 6 15 m 30 15 l S')
        check('round-join-analytic','1 j 6 w 9 6 m 9 21 l 30 21 l S',
              expected=lambda x,y:(6<=x<=12 and 6<=y<=21) or (9<=x<=30 and 18<=y<=24) or (x-9)**2+(y-21)**2<=9,reference=False)
        check('zero-gap','[6 0] 0 d 3 w 6 15 m 30 15 l S',equivalent='3 w 6 15 m 30 15 l S')
        check('round-zero-dashes','[0 6] 0 d 1 J 3 w 6 15 m 30 15 l S',
              expected=lambda x,y:min((x-at)**2+(y-15)**2 for at in (6,12,18,24,30))<=2.25,reference=False)
        check('closed-dash-endpoints','[100 1] 0 d 6 w 9 9 18 12 re S',equivalent='6 w 9 9 m 27 9 l 27 21 l 9 21 l 9 9 l S',secondary=True)
        check('redundant-corner-point','6 w 9 6 m 9 21 l 9 21 l 30 21 l S',equivalent='6 w 9 6 m 9 21 l 30 21 l S',secondary=True)
        check('redundant-start','6 w 9 6 m 9 6 l 9 21 l 30 21 l S',equivalent='6 w 9 6 m 9 21 l 30 21 l S',secondary=True)
        check('scaled-hairline-dashes','2 0 0 1 0 0 cm [3 1.5] 0 d 0 w 3 15.375 m 15 15.375 l S',
              equivalent='[6 3] 0 d 0 w 6 15.375 m 30 15.375 l S',reference=False)
        check('negative-phase','[6 3] -1 d 3 w 6 15 m 30 15 l S',equivalent='[6 3] 8 d 3 w 6 15 m 30 15 l S',reference=False)
        check('reflected-stroke','-1 0 0 1 36 0 cm 2 J 3 w 6 15 m 30 15 l S',equivalent='2 J 3 w 6 15 m 30 15 l S')
        check('dash-work-limit','[.000001 .000001] 0 d 3 w 6 15 m 30 15 l S',reject=True)
        check('indirect-style','/Style gs 6 15 m 30 15 l S',
              resources='/ExtGState << /Style << /LC 5 0 R /D 6 0 R >> >>',
              extra={5:b'2',6:b'[7 0 R 8 0 R]',7:b'[9 0 R 3]',8:b'0',9:b'6'},
              equivalent='2 J [6 3] 0 d 6 15 m 30 15 l S')
        check('indirect-style-cycle','/Style gs',resources='/ExtGState << /Style << /D 5 0 R >> >>',
              extra={5:b'6 0 R',6:b'5 0 R'},reject=True)
        check('indirect-array-cycle','/Style gs',resources='/ExtGState << /Style << /D [5 0 R 0] >> >>',
              extra={5:b'5 0 R'},reject=True)
        check('straight-cubic-dashes','[6 3] 0 d 3 w 6 15 m 12 15 24 15 30 15 c S',equivalent='[6 3] 0 d 3 w 6 15 m 30 15 l S')
        check('odd-array-double-cycle','[3 6 9] 7 d 3 w 6 15 m 30 15 l S',equivalent='[3 6 9 3 6 9] 7 d 3 w 6 15 m 30 15 l S',reference=False)
        check('dash-comment','[6 % dash\n3] 0 d 3 w 6 15 m 30 15 l S',equivalent='[6 3] 0 d 3 w 6 15 m 30 15 l S')
        check('dash-clear','[6 3] 0 d [] 9 d 3 w 6 15 m 30 15 l S',equivalent='3 w 6 15 m 30 15 l S')
        check('sheared-stroke','1 0 .5 1 0 0 cm 3 w 9 6 m 9 24 l S',equivalent='10.5 6 m 13.5 6 l 22.5 24 l 19.5 24 l h f',reference=False)
        check('indirect-bad-phase','/Style gs',resources='/ExtGState << /Style << /D [[3] 5 0 R] >> >>',extra={5:b'/Bad'},reject=True)
        for key,value in (('LC','3'),('LJ','-1'),('ML','.5'),('D','[[0 0] 0]'),('D','[[3 -1] 0]'),('D','[[3] 0 1]')):
            check('invalid-dictionary','/Bad gs 9 15 m 27 15 l S',resources='/ExtGState << /Bad << /'+key+' '+value+' >> >>',reject=True)
        for bad in ('-1 w','3 J','-1 j','1.5 J','.5 M','[0 0] 0 d','[-1 3] 0 d','[true] 0 d','[1] (bad) d','['+'1 '*33+'] 0 d'):
            check('invalid',bad+' 9 15 m 27 15 l S',reject=True)
    print(f'PDF strokes: {count} checks passed; {refs} independent comparisons')
if __name__=='__main__':main()
