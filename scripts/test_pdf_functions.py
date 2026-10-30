#!/usr/bin/env python3
"""Test stitching functions, component arrays, domain edges, and tree budgets."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_shading import shading_pdf
from audit_pdf_corpus import read_ppm

LEAF='<< /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >>'
STITCH='<< /FunctionType 3 /Domain [0 1] /Functions [7 0 R] /Bounds [] /Encode [0 1] >>'
AXIAL='<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Function 6 0 R >>'


def main():
    probe=str(Path(sys.argv[1]).resolve());count=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-functions-')as directory:
        def check(name,function,expected=None,extra=None,shading=AXIAL,reject=False,size=(18,12),reference=False,reference_limit=1):
            nonlocal count
            count+=1;path=Path(directory)/(name+'.pdf')
            objects={7:LEAF};objects.update(extra or {})
            path.write_bytes(shading_pdf(shading,function,size=size,extra=objects))
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert run.returncode==(3 if reject else 0),(name,run.stderr)
            if reject:return
            w,h,pages=struct.unpack('=III',run.stdout[:12]);assert(w,h,pages)==(round(size[0]*4/3),round(size[1]*4/3),1)
            if expected:
                wanted=bytes(c for y in range(h)for x in range(w)for c in (*expected(x,y,w,h),255))
                assert run.stdout[12:]==wanted,(name,next((i,a,b)for i,(a,b)in enumerate(zip(run.stdout[12:],wanted))if a!=b))
            if reference and '--reference'in sys.argv:
                prefix=Path(directory)/'reference'
                subprocess.run(['pdftoppm','-singlefile','-r','96','-aaVector','no',str(path),str(prefix)],capture_output=True,check=True,timeout=15)
                rw,rh,rgb=read_ppm(prefix.with_suffix('.ppm'));assert(rw,rh)==(w,h)
                actual=bytes(c for i,c in enumerate(run.stdout[12:])if i%4!=3)
                error=max(abs(a-b)for a,b in zip(actual,rgb))
                assert error<=reference_limit,(name,'reference differs',error)
        rgb=lambda t:(int(255*(1-t)+.5),0,int(255*t+.5))
        expected=lambda x,y,w,h:rgb((x+.5)/w)
        check('single',STITCH,expected)
        check('reverse',STITCH.replace('[0 1] >>','[1 0] >>'),lambda x,y,w,h:rgb(1-(x+.5)/w))
        pair=STITCH.replace('[7 0 R]','[7 0 R 7 0 R]').replace('/Bounds []','/Bounds [.5]').replace('/Encode [0 1]','/Encode [0 1 1 0]')
        check('two-pieces',pair,lambda x,y,w,h:rgb(2*(x+.5)/w if x<w/2 else 2*(1-(x+.5)/w)))
        check('inline-functions',pair.replace('[7 0 R 7 0 R]','['+LEAF+' '+LEAF+']'),lambda x,y,w,h:rgb(2*(x+.5)/w if x<w/2 else 2*(1-(x+.5)/w)))
        check('indirect-functions-array',STITCH.replace('[7 0 R]','8 0 R'),expected,extra={8:'[7 0 R]'})
        check('nested-stitch',STITCH,expected,extra={7:STITCH.replace('7 0 R','8 0 R'),8:LEAF})
        red='<< /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>'
        zero='<< /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [0] /N 1 >>'
        blue='<< /FunctionType 2 /Domain [0 1] /N 1 >>'
        check('component-array','[7 0 R 8 0 R 9 0 R]',expected,extra={7:red,8:zero,9:blue})
        check('inline-component-array','['+red+' '+zero+' '+blue+']',expected)
        constant='<< /FunctionType 2 /Domain [0 1] /C0 [0 1 0] /C1 [0 1 0] /N 1 >>'
        check('last-bound-equals-domain',pair.replace('/Bounds [.5]','/Bounds [1]').replace('[7 0 R 7 0 R]','[7 0 R 8 0 R]'),
              lambda x,y,w,h:rgb((x+.5)/12)if x<12 else(0,255,0),extra={8:constant},
              shading=AXIAL.replace('[0 0 18 0]','[0 0 9 0]').replace('/Function','/Extend [true true] /Function'))
        check('zero-domain-single',STITCH.replace('/Domain [0 1]','/Domain [0 0]').replace('/Encode [0 1]','/Encode [.25 .75]'),
              lambda x,y,w,h:rgb(.25),shading=AXIAL.replace('/Function','/Domain [0 0] /Function'))
        check('zero-domain-exponential',LEAF.replace('/Domain [0 1]','/Domain [.25 .25]'),lambda x,y,w,h:rgb(.25),
              shading=AXIAL.replace('/Function','/Domain [.25 .25] /Function'))
        check('range-clipping',STITCH.replace('/Bounds','/Range [.25 .75 0 0 .25 .75] /Bounds'),
              lambda x,y,w,h:tuple(int(max(.25,min(.75,c/255))*255+.5)if i!=1 else 0 for i,c in enumerate(expected(x,y,w,h))))
        check('encode-clamps-child-domain',STITCH.replace('/Encode [0 1]','/Encode [-1 2]'),lambda x,y,w,h:rgb(max(0,min(1,3*(x+.5)/w-1))))
        for name,function,extra in (
            ('cycle',STITCH.replace('7 0 R','6 0 R'),{}),
            ('empty-functions',STITCH.replace('[7 0 R]','[]'),{}),
            ('missing-bound',pair.replace('/Bounds [.5]','/Bounds []'),{}),
            ('invalid-bound',pair.replace('/Bounds [.5]','/Bounds [-1]'),{}),
            ('short-encode',pair.replace('/Encode [0 1 1 0]','/Encode [0 1]'),{}),
            ('component-count','[7 0 R 7 0 R]',{7:red}),
            ('bad-child-components',STITCH,{7:red}),
            ('array-cycle',STITCH.replace('[7 0 R]','8 0 R'),{8:'8 0 R'}),
        ):check(name,function,extra=extra,reject=True)
        for parts in (31,32):
            # 1 + parts * 2 nodes: 63 fits, 65 exceeds the shared node budget.
            children=' '.join('7 0 R'for _ in range(parts))
            bounds=' '.join(str(i/parts)for i in range(1,parts))
            encoded=' '.join('0 1'for _ in range(parts))
            root=f'<< /FunctionType 3 /Domain [0 1] /Functions [{children}] /Bounds [{bounds}] /Encode [{encoded}] >>'
            check('node-budget-'+str(parts),root,lambda x,y,w,h:(0,255,0),
                  extra={7:STITCH.replace('7 0 R','8 0 R'),8:constant},reject=parts==32)
        large=AXIAL.replace('[0 0 18 0]','[0 0 450 0]')
        check('reference-stitch',pair,lambda x,y,w,h:rgb(2*(x+.5)/w if x<w/2 else 2*(1-(x+.5)/w)),shading=large,size=(450,300),reference=True,reference_limit=2)
        check('reference-array','[7 0 R 8 0 R 9 0 R]',expected,extra={7:red,8:zero,9:blue},shading=large,size=(450,300),reference=True)
    print(f'PDF color functions: {count} passed')


if __name__=='__main__':main()
