#!/usr/bin/env python3
"""Exercise Lab component ranges, sample decoding and source-space composition."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_type3 import stream
from test_pdf_calibrated import D65,D50,M65,M50,encode
from audit_pdf_corpus import read_ppm
from test_pdf_embedded_fonts import fixture as text_fixture
from test_pdf_mesh import fixture as mesh_fixture


def lab_from_linear(values, matrix, white):
    matrix=list(map(float,matrix.split()));white=list(map(float,white.split()))
    xyz=[sum(matrix[i+3*j]*values[j] for j in range(3))/white[i] for i in range(3)]
    def f(value):
        return value**(1/3) if value > 216/24389 else (24389/27*value+16)/116
    x,y,z=map(f,xyz)
    return (116*y-16,500*(x-y),200*(y-z))


def main():
    probe,allocator=[str(Path(arg).resolve()) for arg in sys.argv[1:3]]
    checks=allocations=references=0
    space='[/Lab << /WhitePoint ['+D65+'] >>]'
    def fixture(content,cs=space,extra=None,resources=''):
        return document('/MediaBox [0 0 18 12] /Resources << /ColorSpace << /C '+cs+' >> '+resources+' >>',stream=content,extra=extra)
    def paint(values):
        return '/C cs '+' '.join(f'{v:.15f}' for v in values)+' sc 0 0 18 12 re f'
    with tempfile.TemporaryDirectory(prefix='cupidimage-lab-') as directory:
        path=Path(directory)/'case.pdf'
        def render(pdf,reject=False):
            nonlocal checks
            path.write_bytes(pdf)
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert run.returncode==(3 if reject else 0),run.stderr
            checks+=1
            return run.stdout
        def same(a,b):
            assert render(a)==render(b),'Lab component decode or paint differs'
        for white,matrix in ((D65,M65),(D50,M50)):
            cs='[/Lab << /WhitePoint ['+white+'] /Range [-128 128 -128 128] >>]'
            for values in ((0,0,0),(1,1,1),(1,0,0),(0,1,0),(0,0,1),(.1,.2,.3),(.25,.5,.75),(.001,.002,.003)):
                lab=lab_from_linear(values,matrix,white)
                expected=tuple(encode(v) for v in values)
                pdf=fixture(paint(lab),cs)
                result=render(pdf)
                assert all(abs(result[i+c]-expected[c]) <= 1 for i in range(12,len(result),4) for c in range(3)),(white,values,lab,result[12:16],expected)
                if '--reference' in sys.argv:
                    prefix=Path(directory)/'reference'
                    run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=15)
                    assert run.returncode==0,run.stderr
                    w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                    assert (w,h)==(24,16) and max(abs(rgb[i]-result[12+(i//3)*4+i%3]) for i in range(len(rgb))) <= 1,(white,values,result[12:16],rgb[:3])
                    references+=1
        for value,clipped in [((-5,0,0),(0,0,0)),((150,0,0),(100,0,0)),((50,-150,150),(50,-100,100))]:
            same(fixture(paint(value)),fixture(paint(clipped)))
        restricted='[/Lab << /WhitePoint ['+D65+'] /Range [-50 -20 10 30] >>]'
        same(fixture('/C cs 0 0 18 12 re f',restricted),fixture(paint((0,-20,10)),restricted))
        same(fixture(paint((50,-100,100)),restricted),fixture(paint((50,-50,30)),restricted))
        for range_value,extra in [('null',None),('10 0 R',{10:'[-100 100 -100 100]'}),
                                  ('[10 0 R 100 -100 100]',{10:'-100'})]:
            alternate='[/Lab << /WhitePoint ['+D65+'] /Range '+range_value+' >>]'
            same(fixture(paint((50,-20,40))),fixture(paint((50,-20,40)),alternate,extra))
        # Decode defaults map samples to L=[0,100] and the declared a/b ranges.
        samples=bytes([128,64,192])
        values=(128/255*100,-50+64/255*30,10+192/255*20)
        attrs='/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace '+restricted
        image=stream(samples,attrs)
        baseline=fixture(paint(values),restricted)
        same(baseline,fixture('18 0 0 12 0 0 cm /Im Do',restricted,{10:image},'/XObject << /Im 10 0 R >>'))
        same(baseline,fixture('18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /C /F /AHx ID 8040c0> EI',restricted))
        indexed='[/Indexed '+restricted+' 0 <8040c0>]'
        same(baseline,fixture('/C cs 0 sc 0 0 18 12 re f',indexed))
        same(baseline,fixture('18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /C /F /AHx ID 00> EI',indexed))
        # Signed palette components also survive mesh interpolation and pattern selection.
        vertices=[(0,0,0,(0,)),(0,1,0,(0,)),(0,0,1,(0,)),(1,1,1,(0,))]
        same(baseline,mesh_fixture(vertices=vertices,space=indexed))
        attrs='/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>'
        same(baseline,fixture('/C cs 0 /P scn 0 0 18 12 re f','[/Pattern '+indexed+']',
                             {10:stream('0 0 3 3 re f',attrs)},'/Pattern << /P 10 0 R >>'))
        page='<< /Type /Page /Parent 2 0 R /MediaBox [0 0 120 90] /Contents 4 0 R /Resources << /Font << /F 5 0 R >> /ColorSpace << /C '+space+' >> >> >>'
        same(text_fixture('/C cs 50 -20 40 sc BT /F 30 Tf 20 30 Td (A) Tj ET',extra={3:page}),
             text_fixture('/C cs 50 -20 40 sc 20 30 12 18 re f',extra={3:page}))
        # Matte unblending is performed in Lab, including signed a/b components.
        def matte_pdf(matte):
            wanted=(50,-20,40);background=(100,30,-10)
            stored=tuple((a+b)*.5 for a,b in zip(wanted,background)) if matte else wanted
            decode=' '.join(str(v) for component in stored for v in (component,component))
            im=stream(bytes(3),'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace '+space+' /Decode ['+decode+'] /SMask 11 0 R')
            mask=stream(b'\xff','/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray /Decode [0 .5]'+(' /Matte [100 30 -10]' if matte else ''))
            return fixture('18 0 0 12 0 0 cm /Im Do',extra={10:im,11:mask},resources='/XObject << /Im 10 0 R >>')
        same(matte_pdf(True),matte_pdf(False))
        def indexed_matte_pdf(indexed):
            stored=(191/255*100,115/255*200-100,153/255*200-100)
            original=(2*stored[0]-100,2*stored[1],2*stored[2])
            if indexed:
                color_space='[/Indexed '+space+' 0 <bf7399>]'
                samples=b'\0';decode=''
            else:
                color_space=space;samples=bytes(3)
                decode=' /Decode ['+' '.join(str(v) for c in original for v in (c,c))+']'
            im=stream(samples,'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace '+color_space+decode+' /SMask 11 0 R')
            mask=stream(b'\xff','/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray /Decode [0 .5]'+(' /Matte [100 0 0]' if indexed else ''))
            return fixture('18 0 0 12 0 0 cm /Im Do',extra={10:im,11:mask},resources='/XObject << /Im 10 0 R >>')
        same(indexed_matte_pdf(True),indexed_matte_pdf(False))
        for bad in ('[1 0 -100 100]','[-100 100 1 0]','[1 2 3]','42','[0 1 /X 2]'):
            render(fixture('/C cs','[/Lab << /WhitePoint ['+D65+'] /Range '+bad+' >>]'),True)
        for pdf in (baseline,indexed_matte_pdf(True),fixture('/C cs 0 sc 0 0 18 12 re f',indexed),matte_pdf(True)):
            path.write_bytes(pdf)
            run=subprocess.run([allocator,str(path),'0'],capture_output=True,text=True,timeout=30)
            assert run.returncode==0,run.stdout+run.stderr
            allocations+=int(run.stdout.split(': ')[1].split(' checks')[0])
    print(f'PDF Lab colors: {checks} checks; {references} independent comparisons; {allocations} allocation checks')


if __name__=='__main__':
    main()
