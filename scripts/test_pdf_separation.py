#!/usr/bin/env python3
"""Check single-colorant source spaces and their alternate tint transforms."""
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_xref import PDF
from test_pdf_icc import profile
from test_pdf_type3 import stream
from test_pdf_embedded_fonts import fixture as text_fixture
from test_pdf_calibrated import RGB, GRAY
from test_pdf_mesh import fixture as mesh_fixture, VERTICES
from audit_pdf_corpus import read_ppm

FUNCTION='<< /FunctionType 2 /Domain [0 1] /C0 [1 1 1] /C1 [1 0 0] /N 1 >>'
SPACE='[/Separation /Spot /DeviceRGB '+FUNCTION+']'


def fixture(content,space=SPACE,extra=None,resources='',spaces=''):
    return document('/MediaBox [0 0 18 12] /Resources << /ColorSpace << /S '+space+' '+spaces+' >> '+resources+' >>',stream=content,extra=extra)


def main():
    probe,allocator=[str(Path(arg).resolve()) for arg in sys.argv[1:3]]
    checks=references=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-separation-') as directory:
        root=Path(directory);path=root/'case.pdf'
        def render(pdf,reject=False):
            nonlocal checks
            path.write_bytes(pdf)
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert run.returncode==(3 if reject else 0),run.stderr
            checks+=1
            return run.stdout
        def same(pdf,expected,reference=False):
            nonlocal references
            actual=render(pdf)
            assert actual==render(expected),'tint rendering differs'
            if reference and '--reference' in sys.argv:
                path.write_bytes(pdf);prefix=root/'reference'
                run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                assert run.returncode==0,run.stderr
                w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                assert (w,h)==(24,16) and max(abs(rgb[i]-actual[12+(i//3)*4+i%3]) for i in range(len(rgb)))<=1
                references+=1
        def fault(pdf):
            nonlocal allocations
            path.write_bytes(pdf)
            run=subprocess.run([allocator,str(path),'0'],capture_output=True,text=True,timeout=40)
            assert run.returncode==0,run.stdout+run.stderr
            allocations+=int(run.stdout.split(': ')[1].split(' checks')[0])
        for tint in (-1,0,.25,.5,1,2):
            value=max(0,min(1,tint))
            expected=fixture(f'1 {1-value} {1-value} rg 0 0 18 12 re f')
            same(fixture(f'/S cs {tint} scn 0 0 18 12 re f'),expected,True)
            same(fixture(f'/S CS {tint} SCN 12 w 0 6 m 18 6 l S'),expected,True)
            same(fixture(f'/S cs {tint} scn 0 0 18 12 re f',space=SPACE.replace('/Spot','/All')),
                 fixture(f'{1-value} g 0 0 18 12 re f'))
        red=fixture('1 0 0 rg 0 0 18 12 re f')
        same(fixture('/S cs 0 0 18 12 re f'),red,True)
        same(fixture('/S cs .5 scn q 0 g Q 0 0 18 12 re f'),fixture('1 .5 .5 rg 0 0 18 12 re f'))
        none=SPACE.replace('/Spot','/None')
        same(fixture('0 0 1 rg 0 0 18 12 re f /S cs 1 scn 0 0 18 12 re f',space=none),fixture('0 0 1 rg 0 0 18 12 re f'),True)
        same(fixture('/S cs 0 0 9 12 re W f 1 0 0 rg 0 0 18 12 re f',space=none),
             fixture('0 0 9 12 re W n 1 0 0 rg 0 0 18 12 re f'),True)
        for op in ('B','B*','b','b*'):
            same(fixture('1 0 0 rg /S CS 20 w 3 3 12 6 re '+op,space=none),fixture('1 0 0 rg 3 3 12 6 re f'),True)
            same(fixture('/S cs 1 0 0 RG 2 w 3 3 12 6 re '+op,space=none),fixture('1 0 0 RG 2 w 3 3 12 6 re S'),True)
        same(fixture('/S cs 1 scn 0 0 18 12 re f',space='[/Separation 10 0 R /DeviceRGB 11 0 R]',extra={10:'/Sp#6ft',11:FUNCTION}),red)
        # A calculator with one tint and three output components.
        calculator=stream('{ 1 exch 1 exch sub dup }','/FunctionType 4 /Domain [0 1] /Range [0 1 0 1 0 1]')
        sampled=stream(bytes([255,255,255,255,0,0]),'/FunctionType 0 /Domain [0 1] /Range [0 1 0 1 0 1] /Size [2] /BitsPerSample 8')
        for function in (calculator,sampled):
            pdf=fixture('/S cs .5 scn 0 0 18 12 re f',space='[/Separation /Spot /DeviceRGB 10 0 R]',extra={10:function})
            same(pdf,fixture('1 .5 .5 rg 0 0 18 12 re f'),True);fault(pdf)
        image=stream(bytes([128]),'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace '+SPACE)
        expected=fixture(f'1 {127/255} {127/255} rg 0 0 18 12 re f')
        same(fixture('18 0 0 12 0 0 cm /Im Do',extra={10:image},resources='/XObject << /Im 10 0 R >>'),expected,True)
        same(fixture('18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /S /F /AHx ID 80> EI'),expected,True)
        same(fixture('/I cs 1 scn 0 0 18 12 re f',spaces='/I [/Indexed '+SPACE+' 1 <00ff>]'),red,True)
        cell=stream('0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>')
        same(fixture('/P cs .5 /Tile scn 0 0 18 12 re f',spaces='/P [/Pattern '+SPACE+']',extra={10:cell},resources='/Pattern << /Tile 10 0 R >>'),fixture('1 .5 .5 rg 0 0 18 12 re f'))
        for space in (SPACE,none):
            page={3:'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 120 90] /Contents 4 0 R /Resources << /Font << /F 5 0 R >> /ColorSpace << /S '+space+' >> >> >>'}
            same(text_fixture('/S cs .5 scn BT /F 30 Tf 20 30 Td (A) Tj ET',extra=page),
                 text_fixture('1 .5 .5 rg 20 30 12 18 re f' if space==SPACE else 'q Q'))
            same(text_fixture('1 0 0 rg /S CS 20 w BT /F 30 Tf 2 Tr 20 30 Td (A) Tj ET',extra={3:page[3].replace(SPACE,none)}),text_fixture('1 0 0 rg 20 30 12 18 re f'))
        same(fixture('/DeviceGray cs .5 scn 0 0 18 12 re f',spaces='/DefaultGray '+SPACE),fixture('1 .5 .5 rg 0 0 18 12 re f'))
        calibrated=SPACE.replace('/DeviceRGB',RGB)
        same(fixture('/DeviceGray cs .5 scn 0 0 18 12 re f',spaces='/DefaultGray '+SPACE+' /DefaultRGB '+RGB),
             fixture('1 .5 .5 rg 0 0 18 12 re f'),True)
        same(fixture('/S cs .5 scn 0 0 18 12 re f',spaces='/DefaultRGB '+RGB),
             fixture('/S cs .5 scn 0 0 18 12 re f',space=calibrated),True)
        scalar=[(f,x,y,(x,)) for f,x,y,_ in VERTICES]
        rgb=[(f,x,y,(1,1-x,1-x)) for f,x,y,_ in VERTICES]
        same(mesh_fixture(vertices=scalar,space=SPACE),mesh_fixture(vertices=rgb))
        same(fixture('18 0 0 12 0 0 cm /Im Do',extra={10:stream(bytes([255]),'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace '+none)},resources='/XObject << /Im 10 0 R >>'),fixture('q Q'))
        same(fixture('/P cs 1 /Tile scn 0 0 18 12 re f',spaces='/P [/Pattern '+none+']',extra={10:cell},resources='/Pattern << /Tile 10 0 R >>'),fixture('q Q'))
        page={3:'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 120 90] /Contents 4 0 R /Resources << /Font << /F 5 0 R >> /ColorSpace << /S '+none+' >> >> >>'}
        same(text_fixture('/S cs BT /F 30 Tf 4 Tr 20 30 Td (A) Tj ET 1 0 0 rg 0 0 120 90 re f',extra=page),text_fixture('20 30 12 18 re W n 1 0 0 rg 0 0 120 90 re f'))
        attrs='/Type /XObject /Subtype /Form /BBox [0 0 18 12] /Resources << /ColorSpace << /S '+SPACE
        forms={10:stream('/S cs .5 scn 0 0 9 12 re f',attrs+' /DefaultRGB '+RGB+' >> >>'),
               11:stream('/S cs .5 scn 9 0 9 12 re f',attrs+' >> >>')}
        mixed=fixture('/F Do /G Do',extra=forms,resources='/XObject << /F 10 0 R /G 11 0 R >>')
        same(mixed,fixture('/C cs 1 .5 .5 scn 0 0 9 12 re f 1 .5 .5 rg 9 0 9 12 re f',spaces='/C '+RGB));fault(mixed)
        # Tint functions stay in source space through gradients and interpolation.
        shading='<< /ShadingType 2 /ColorSpace '+SPACE.replace('/N 1','/N 2')+' /Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [1] /N 1 >> /Extend [true true] >>'
        oracle='<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Function '+FUNCTION.replace('/N 1','/N 2')+' /Extend [true true] >>'
        same(fixture('/Sh sh',extra={10:shading},resources='/Shading << /Sh 10 0 R >>'),
             fixture('/Sh sh',extra={10:oracle},resources='/Shading << /Sh 10 0 R >>'))
        stitching='<< /FunctionType 3 /Domain [0 1] /Functions ['+FUNCTION+' '+FUNCTION+'] /Bounds [.5] /Encode [0 1 0 1] >>'
        same(fixture('/S cs .75 scn 0 0 18 12 re f',space='[/Separation /Spot /DeviceRGB '+stitching+']'),fixture('1 .5 .5 rg 0 0 18 12 re f'),True)
        icc_objects={10:stream(profile(),'/N 3')}
        icc_space=SPACE.replace('/DeviceRGB','[/ICCBased 10 0 R]')
        same(fixture('/S cs .5 scn 0 0 18 12 re f',space=icc_space,extra=icc_objects),
             fixture('/I cs 1 .5 .5 scn 0 0 18 12 re f',extra=icc_objects,spaces='/I [/ICCBased 10 0 R]'))
        fault(fixture('/S cs .5 scn 0 0 18 12 re f',space=icc_space,extra=icc_objects))
        # Repeated identical definitions do not consume the resource cache limit.
        same(fixture(('/S cs '*1000)+'1 scn 0 0 18 12 re f'),red)
        for count,reject in ((256,False),(257,True)):
            spaces=' '.join('/C'+str(i)+' '+SPACE.replace('/Spot','/Spot'+str(i)) for i in range(count))
            render(fixture(' '.join('/C'+str(i)+' cs' for i in range(count)),spaces=spaces),reject)
        for extra_length,reject in ((1024*1024-len(SPACE)-4,False),(1024*1024,True)):
            long_space=SPACE.replace('/Spot','/Spot %'+'x'*extra_length+'\n')
            render(fixture('/S cs 1 scn 0 0 18 12 re f',space=long_space),reject)
        # Function streams retain the xref context for compressed metadata.
        encoded=zlib.compress(b'{ 1 exch 1 exch sub dup }')
        packed={20:b'4',21:b'[0 1]',22:b'[0 1 0 1 0 1]',23:str(len(encoded)).encode(),24:b'/FlateDecode',25:b'[/Separation /Spot /DeviceRGB 10 0 R]'}
        header='';body=bytearray()
        for number,value in packed.items():
            header+=f'{number} {len(body)} ';body+=value+b' '
        pdf=PDF();pdf.obj(1,b'<< /Type /Catalog /Pages 2 0 R >>');pdf.obj(2,b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>')
        pdf.obj(3,b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Resources << /ColorSpace << /S 25 0 R >> >> >>')
        pdf.obj(4,stream('/S cs .5 scn 0 0 18 12 re f'))
        pdf.obj(10,b'<< /FunctionType 20 0 R /Domain 21 0 R /Range 22 0 R /Length 23 0 R /Filter 24 0 R >>\nstream\n'+encoded+b'\nendstream')
        pdf.obj(15,stream(zlib.compress(header.encode()+body),f'/Type /ObjStm /N {len(packed)} /First {len(header)} /Filter /FlateDecode'))
        entries={0:(0,0,65535),**pdf.objects};entries.update({number:(2,15,index) for index,number in enumerate(packed)})
        pdf.xref(30,entries=entries);compressed=bytes(pdf.data)
        same(compressed,fixture('1 .5 .5 rg 0 0 18 12 re f'));fault(compressed)
        for invalid in ('[/Separation /Spot /Pattern '+FUNCTION+']','[/Separation /Spot [/Indexed /DeviceRGB 0 <000000>] '+FUNCTION+']',
                        '[/Separation /Spot '+SPACE+' '+FUNCTION+']','[/Separation /Spot /DeviceRGB 42]',
                        '[/Separation (Spot) /DeviceRGB '+FUNCTION+']',SPACE[:-1],SPACE[:-1]+' 0]'):
            render(fixture('/S cs 1 scn 0 0 18 12 re f',space=invalid),True)
        for pdf in (fixture('/S cs .5 scn 0 0 18 12 re f'),fixture('/S cs 0 0 18 12 re f',space=none),
                    fixture('/S cs .5 scn 0 0 18 12 re f',space=calibrated)):
            fault(pdf)
    print(f'PDF Separation colors: {checks} checks; {references} independent comparisons; {allocations} allocation checks')


if __name__=='__main__':
    main()
