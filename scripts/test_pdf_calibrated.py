#!/usr/bin/env python3
"""Check calibrated source colors against analytic sRGB values and paired geometry."""
from pathlib import Path
import math
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_type3 import stream
from test_pdf_embedded_fonts import fixture as text_fixture
from audit_pdf_corpus import read_ppm
from test_pdf_xref import PDF
from test_pdf_mesh import fixture as mesh_fixture
from test_pdf_patch import fixture as patch_fixture, control_points

D65 = '.95047 1 1.08883'
D50 = '.96422 1 .82521'
M65 = '.4124564 .2126729 .0193339 .3575761 .7151522 .1191920 .1804375 .0721750 .9503041'
M50 = '.4360747 .2225045 .0139322 .3850649 .7168786 .0971045 .1430804 .0606169 .7141733'
GRAY = '[/CalGray << /WhitePoint ['+D65+'] >>]'
RGB = '[/CalRGB << /WhitePoint ['+D65+'] /Matrix ['+M65+'] >>]'


def encode(value):
    value = min(1, max(0, value))
    return round(255*(12.92*value if value <= .0031308 else 1.055*value**(1/2.4)-.055))


def main():
    probe = str(Path(sys.argv[1]).resolve())
    allocator = str(Path(sys.argv[2]).resolve())
    checks = allocations = references = 0
    def fixture(space, content, extra=None, resources=''):
        return document('/MediaBox [0 0 18 12] /Resources << /ColorSpace << /C '+space+
                        ' >> '+resources+' >>', stream=content, extra=extra)
    with tempfile.TemporaryDirectory(prefix='cupidimage-calibrated-') as directory:
        path = Path(directory)/'case.pdf'
        def render(data, reject=False):
            nonlocal checks
            path.write_bytes(data)
            run = subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert run.returncode == (3 if reject else 0), run.stderr
            checks += 1
            return run.stdout
        def solid(data, expected, reference=True):
            nonlocal references
            result = render(data)
            assert struct.unpack('=III',result[:12]) == (24,16,1)
            for i in range(12,len(result),4):
                assert result[i+3] == 255 and all(abs(result[i+c]-expected[c]) <= 1 for c in range(3)), (result[i:i+4],expected)
            if reference and '--reference' in sys.argv:
                prefix=Path(directory)/'reference'
                run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=15)
                assert run.returncode == 0, run.stderr
                w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                assert (w,h)==(24,16) and max(abs(rgb[i]-result[12+(i//3)*4+i%3]) for i in range(len(rgb))) <= 1, (expected,result[12:16],rgb[:3],data)
                references += 1
        def same(a,b):
            assert render(a) == render(b), 'calibrated source lost across pipeline/state'
        for white in (D65,D50,'1 1 1'):
            for gamma in (.5,1,2.2,3):
                space=f'[/CalGray << /WhitePoint [{white}] /Gamma {gamma} >>]'
                for value in (-1,0,.003,.25,.5,1,2):
                    expected=encode(min(1,max(0,value))**gamma)
                    solid(fixture(space,f'/C cs {value} sc 0 0 18 12 re f'),(expected,)*3)
        for white,matrix in ((D65,M65),(D50,M50)):
            for gamma in ('1 1 1','2.2 1 .5'):
                gs=[float(v) for v in gamma.split()]
                space=f'[/CalRGB << /WhitePoint [{white}] /Matrix [{matrix}] /Gamma [{gamma}] >>]'
                for values in ((1,0,0),(0,1,0),(0,0,1),(.25,.5,.75),(0,0,0),(1,1,1),(-1,2,.5)):
                    expected=tuple(encode(min(1,max(0,v))**g) for v,g in zip(values,gs))
                    solid(fixture(space,'/C cs '+' '.join(map(str,values))+' sc 0 0 18 12 re f'),expected,reference=all(0 <= v <= 1 for v in values))
        # Null defaults and numeric references are read as PDF objects, not text guesses.
        baseline=fixture(GRAY,'/C cs .5 sc 0 0 18 12 re f')
        for params,extra in [('/WhitePoint ['+D65+'] /Gamma null /BlackPoint null',None),
                             ('/WhitePoint 10 0 R /Gamma 11 0 R',{10:'[.95047 1 1.08883]',11:'1'}),
                             ('/WhitePoint [10 0 R 1 1.08883]',{10:'.95047'})]:
            same(baseline,fixture('[/CalGray << '+params+' >>]','/C cs .5 sc 0 0 18 12 re f',extra))
        same(baseline,fixture('[/CalGray 10 0 R]','/C cs .5 sc 0 0 18 12 re f',{10:'<< /WhitePoint ['+D65+'] >>'}))
        same(baseline,fixture(GRAY,'/C cs .5 sc q 0 g Q 0 0 18 12 re f'))
        same(fixture('/DeviceGray','.5 g 0 0 18 12 re f'),fixture(GRAY,'/C cs .9 sc .5 g 0 0 18 12 re f'))
        same(baseline,fixture(GRAY,'/C CS .5 SC 12 w 0 6 m 18 6 l S'))
        sample=128/255
        expected=(encode(sample),)*3
        image=stream(bytes([128]),'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace '+GRAY)
        solid(fixture(GRAY,'18 0 0 12 0 0 cm /Im Do',{10:image},'/XObject << /Im 10 0 R >>'),expected)
        solid(fixture(GRAY,'18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /C /F /AHx ID 80> EI'),expected)
        indexed='[/Indexed '+GRAY+' 1 <0080>]'
        solid(fixture(indexed,'/C cs 1 sc 0 0 18 12 re f'),expected)
        solid(fixture(indexed,'18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /C /F /AHx ID 01> EI'),expected)
        # Compressed indirect calibration values retain their xref context in inline images.
        packed={10:('[/CalGray << /WhitePoint 11 0 R /Gamma 12 0 R >>]').encode(),
                11:('['+D65+']').encode(),12:b'1'}
        body=bytearray(); header=''
        for number,value in packed.items():
            header+=f'{number} {len(body)} '; body+=value+b' '
        pdf=PDF()
        pdf.obj(1,b'<< /Type /Catalog /Pages 2 0 R >>')
        pdf.obj(2,b'<< /Type /Pages /Count 1 /Kids [3 0 R] >>')
        pdf.obj(3,b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Resources << /ColorSpace << /C 10 0 R >> >> >>')
        pdf.obj(4,stream('18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /C /F /AHx ID 80> EI'))
        pdf.obj(15,stream(zlib.compress(header.encode()+body),f'/Type /ObjStm /N 3 /First {len(header)} /Filter /FlateDecode'))
        entries={0:(0,0,65535),**pdf.objects}
        entries.update({number:(2,15,index) for index,number in enumerate(packed)})
        pdf.xref(16,entries=entries)
        compressed_inline=bytes(pdf.data)
        solid(compressed_inline,expected)
        for kind in (4,5):
            vertices=[(0,0,0,(sample,)),(0,1,0,(sample,)),(0,0,1,(sample,)),(1,1,1,(sample,))]
            solid(mesh_fixture(vertices=vertices,kind=kind,space=GRAY),expected,reference=False)
        for kind in (6,7):
            patches=[(0,control_points(kind),[(sample,)]*4)]
            solid(patch_fixture(kind=kind,patches=patches,space=GRAY),expected,reference=False)
        same(baseline,fixture('[/CalRGB << /WhitePoint [1 1 1] /Gamma null /Matrix null >>]',
                             '/C cs .5 .5 .5 sc 0 0 18 12 re f'))
        # Native glyphs use the same source conversion as paths, without changing advances.
        page='<< /Type /Page /Parent 2 0 R /MediaBox [0 0 120 90] /Contents 4 0 R /Resources << /Font << /F 5 0 R >> /ColorSpace << /C '+GRAY+' >> >> >>'
        text='/C cs .5 sc BT /F 30 Tf 20 30 Td (A) Tj ET'
        same(text_fixture(text,extra={3:page}),text_fixture('/C cs .5 sc 20 30 12 18 re f',extra={3:page}))
        # Colored and uncolored tiling, including backdrop-dependent cells.
        for deferred in (False,True):
            cell=('/G gs ' if deferred else '')+'0 0 3 3 re f'
            attrs='/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << /ExtGState << /G << /BM /Multiply >> >> >>'
            data=fixture('[/Pattern '+GRAY+']','/C cs .5 /P scn 0 0 18 12 re f',
                         {10:stream(cell,attrs)},'/Pattern << /P 10 0 R >>')
            solid(data,(encode(.5),)*3,reference=False)
        shade='<< /ShadingType 2 /ColorSpace '+GRAY+' /Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [.5] /C1 [.5] /N 1 >> /Extend [true true] >>'
        solid(fixture(GRAY,'/S sh',{10:shade},'/Shading << /S 10 0 R >>'),(encode(.5),)*3)
        bad=['<< >>','<< /WhitePoint [0 1 1] >>','<< /WhitePoint [1 2 1] >>',
             '<< /WhitePoint [1 1 -1] >>','<< /WhitePoint [1 1] >>',
             '<< /WhitePoint [1 1 1] /Gamma 0 >>','<< /WhitePoint [1 1 1] /Gamma -1 >>',
             '<< /WhitePoint [1 1 1] /Gamma [] >>','<< /WhitePoint [1 1 1] /BlackPoint [-1 0 0] >>',
             '<< /WhitePoint [1 1 1] /BlackPoint [.1 .1 .1] >>','42','null']
        for params in bad:
            render(fixture('[/CalGray '+params+']','/C cs .5 sc 0 0 18 12 re f'),True)
        render(fixture(GRAY,'',extra={3:'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Group << /S /Transparency /CS '+GRAY+' >> >>'}),True)
        for entry in ('/Gamma [1 0 1]','/Gamma [1 1]','/Matrix [1 0 0]','/Gamma 2'):
            render(fixture('[/CalRGB << /WhitePoint ['+D65+'] '+entry+' >>]','/C cs'),True)
        for data in (baseline,compressed_inline,fixture(RGB,'/C cs .2 .4 .6 sc 0 0 18 12 re f'),
                     fixture(GRAY,'18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /C /F /AHx ID 80> EI')):
            path.write_bytes(data)
            run=subprocess.run([allocator,str(path),'0'],capture_output=True,text=True,timeout=30)
            assert run.returncode == 0,run.stdout+run.stderr
            allocations += int(run.stdout.split(': ')[1].split(' checks')[0])
    print(f'PDF calibrated colors: {checks} checks; {references} independent comparisons; {allocations} allocation checks')


if __name__ == '__main__':
    main()
