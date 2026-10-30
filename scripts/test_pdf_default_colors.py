#!/usr/bin/env python3
"""Verify resource-scoped device-color remapping without recursively remapping defaults."""
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_xref import PDF
from test_pdf_type3 import stream
from test_pdf_calibrated import GRAY,RGB
from test_pdf_icc import profile,curve
from test_pdf_embedded_fonts import fixture as text_fixture
from test_pdf_mesh import fixture as mesh_fixture,VERTICES
from audit_pdf_corpus import read_ppm


def main():
    probe,allocator=[str(Path(arg).resolve()) for arg in sys.argv[1:3]]
    checks=references=allocations=0
    def fixture(content,default_rgb=RGB,default_gray=GRAY,extra=None,spaces='',resources=''):
        colors='/C '+RGB+' /G '+GRAY+' '+spaces
        if default_rgb is not None:colors+=' /DefaultRGB '+default_rgb
        if default_gray is not None:colors+=' /DefaultGray '+default_gray
        return document('/MediaBox [0 0 18 12] /Resources << /ColorSpace << '+colors+' >> '+resources+' >>',stream=content,extra=extra)
    with tempfile.TemporaryDirectory(prefix='cupidimage-default-colors-') as directory:
        path=Path(directory)/'case.pdf'
        def render(pdf,reject=False):
            nonlocal checks
            path.write_bytes(pdf)
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert run.returncode==(3 if reject else 0),run.stderr
            checks+=1
            return run.stdout
        def same(pdf,oracle,reference=False):
            nonlocal references
            actual=render(pdf);expected=render(oracle)
            assert actual==expected,'default color selection/scope differs'
            if reference and '--reference' in sys.argv:
                path.write_bytes(pdf);prefix=Path(directory)/'reference'
                run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=15)
                assert run.returncode==0,run.stderr
                w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                assert (w,h)==(24,16) and max(abs(rgb[i]-actual[12+(i//3)*4+i%3]) for i in range(len(rgb)))<=1
                references+=1
        white=fixture('q Q')
        for content in ('/Pattern cs 0 0 18 12 re f','/Pattern CS 12 w 0 6 m 18 6 l S',
                        '/PC cs 0 0 18 12 re f','/Pattern cs q .25 .5 .75 rg Q 0 0 18 12 re f'):
            same(fixture(content,spaces='/PC [/Pattern /DeviceRGB]'),white,True)
        same(fixture('/Pattern cs 0 0 9 12 re W f 1 0 0 rg 0 0 18 12 re f'),
             fixture('0 0 9 12 re W n 1 0 0 rg 0 0 18 12 re f'),True)
        for op in ('B','B*','b','b*'):
            same(fixture('.25 .5 .75 rg /Pattern CS 20 w 3 3 12 6 re '+op),
                 fixture('.25 .5 .75 rg 3 3 12 6 re f'),True)
            same(fixture('/Pattern cs .25 .5 .75 RG 2 w 3 3 12 6 re '+op),
                 fixture('.25 .5 .75 RG 2 w 3 3 12 6 re S'),True)
        rgb='/C cs .25 .5 .75 sc 0 0 18 12 re f'
        gray='/G cs .5 sc 0 0 18 12 re f'
        for content in ('.25 .5 .75 rg 0 0 18 12 re f','/DeviceRGB cs .25 .5 .75 sc 0 0 18 12 re f',
                        '/Device#52GB cs .25 .5 .75 scn 0 0 18 12 re f',
                        '.25 .5 .75 RG 12 w 0 6 m 18 6 l S',
                        '/DeviceRGB CS .25 .5 .75 SC 12 w 0 6 m 18 6 l S'):
            same(fixture(content),fixture(rgb),True)
        for content in ('.5 g 0 0 18 12 re f','/DeviceGray cs .5 sc 0 0 18 12 re f',
                        '.5 G 12 w 0 6 m 18 6 l S','/DeviceGray CS .5 SCN 12 w 0 6 m 18 6 l S'):
            same(fixture(content),fixture(gray),True)
        same(fixture('.25 .5 .75 rg q .1 g Q 0 0 18 12 re f'),fixture(rgb))
        raw='.25 .5 .75 rg 0 0 18 12 re f'
        for default,extra in (('null',None),('/DeviceRGB',None),('[/DeviceRGB]',None),
                              ('13 0 R',{13:'null'}),('99 0 R',None)):
            same(fixture(raw,default_rgb=default,extra=extra),fixture(raw,default_rgb=None))
        same(fixture(raw,default_rgb='13 0 R',extra={13:RGB}),fixture(rgb))
        same(fixture('0 1 1 0 k 0 0 18 12 re f',spaces='/DefaultCMYK [/DeviceCMYK]'),
             fixture('0 1 1 0 k 0 0 18 12 re f'))
        for bad in (GRAY,'[/Lab << /WhitePoint [1 1 1] >>]','[/Indexed /DeviceRGB 0 <000000>]','[/Pattern /DeviceRGB]','42','[]'):
            render(fixture(raw,default_rgb=bad),True)
        render(fixture(raw,default_rgb='13 0 R',extra={13:'14 0 R',14:'13 0 R'}),True)
        # An unused default for a different device space is not parsed as the current space.
        same(fixture(gray,spaces='/DefaultCMYK [/Unsupported]'),fixture(gray))
        # Forms inherit the selected graphics state, but new selections use their own resources.
        program='.25 .5 .75 rg 0 0 18 12 re f'
        attrs='/Type /XObject /Subtype /Form /BBox [0 0 18 12]'
        for resources,want in (('',fixture(rgb)),('/Resources << >>',fixture(raw,default_rgb=None)),
                               ('/Resources << /ColorSpace << /DefaultRGB '+RGB+' >> >>',fixture(rgb))):
            same(fixture('/F Do',extra={5:stream(program,attrs+' '+resources)},resources='/XObject << /F 5 0 R >>'),want)
        # A group without CS inherits its parent blending space; its painted sources still remap.
        group=stream(program,attrs+' /Group << /S /Transparency /I true >> /Resources << /ColorSpace << /DefaultRGB '+RGB+' >> >>')
        same(fixture('/F Do',extra={5:group},resources='/XObject << /F 5 0 R >>'),fixture(rgb))
        sample=bytes([64,128,192]);values=' '.join(str(v/255) for v in sample)
        expected=fixture('/C cs '+values+' sc 0 0 18 12 re f')
        imattrs='/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB'
        same(fixture('18 0 0 12 0 0 cm /Im Do',extra={5:stream(sample,imattrs)},resources='/XObject << /Im 5 0 R >>'),expected,True)
        for name,spaces in (('/RGB',''),('/Alias','/Alias /DeviceRGB'),('/Alias','/Alias [/DeviceRGB]')):
            same(fixture('18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS '+name+' /F /AHx ID 4080c0> EI',spaces=spaces),expected,True)
        same(fixture('/I cs 0 sc 0 0 18 12 re f',spaces='/I [/Indexed /DeviceRGB 0 <4080c0>]'),expected)
        # Soft-mask gray samples are alpha values, not DefaultGray colors.
        mask=stream(b'\x80','/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray')
        masked={5:stream(sample,imattrs+' /SMask 6 0 R'),6:mask}
        content='18 0 0 12 0 0 cm /Im Do';resources='/XObject << /Im 5 0 R >>'
        same(fixture(content,extra=masked,resources=resources),fixture(content,default_gray=None,extra=masked,resources=resources))
        cell=stream('0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>')
        same(fixture('/PC cs .25 .5 .75 /P scn 0 0 18 12 re f',spaces='/PC [/Pattern /DeviceRGB]',
                     extra={5:cell},resources='/Pattern << /P 5 0 R >>'),fixture(rgb))
        shade='<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [.25 .5 .75] /C1 [.25 .5 .75] /N 1 >> /Extend [true true] >>'
        same(fixture('/S sh',extra={5:shade},resources='/Shading << /S 5 0 R >>'),fixture(rgb),True)
        mesh_resources='/ColorSpace << /DefaultRGB '+RGB+' >>'
        same(mesh_fixture(resources=mesh_resources),mesh_fixture(space=RGB))
        page='<< /Type /Page /Parent 2 0 R /MediaBox [0 0 120 90] /Contents 4 0 R /Resources << /Font << /F 5 0 R >> /ColorSpace << /DefaultRGB '+RGB+' /C '+RGB+' >> >> >>'
        same(text_fixture('.25 .5 .75 rg BT /F 30 Tf 20 30 Td (A) Tj ET',extra={3:page}),
             text_fixture('/C cs .25 .5 .75 sc 20 30 12 18 re f',extra={3:page}))
        same(text_fixture('/Pattern cs BT /F 30 Tf 20 30 Td (A) Tj ET'),text_fixture('q Q'))
        same(text_fixture('/Pattern cs BT /F 30 Tf 4 Tr 20 30 Td (A) Tj ET 1 0 0 rg 0 0 120 90 re f'),
             text_fixture('20 30 12 18 re W n 1 0 0 rg 0 0 120 90 re f'))
        for make_text in (text_fixture, lambda content: document('/MediaBox [0 0 120 90]',stream=content)):
            same(make_text('.25 .5 .75 rg /Pattern CS 20 w BT /F 30 Tf 2 Tr 20 30 Td (A) Tj ET'),
                 make_text('.25 .5 .75 rg BT /F 30 Tf 0 Tr 20 30 Td (A) Tj ET'))
        icc=profile([curve()]*3)
        objects={10:stream(zlib.compress(icc),'/N 3 /Filter /FlateDecode')}
        same(fixture(raw,default_rgb='[/ICCBased 10 0 R]',extra=objects),
             fixture('/I cs .25 .5 .75 sc 0 0 18 12 re f',extra=objects,spaces='/I [/ICCBased 10 0 R]'))
        for resources in ('<< /ColorSpace 42 >>','<< /ColorSpace 20 0 R >>'):
            render(document('/MediaBox [0 0 18 12] /Resources '+resources,stream=raw,
                            extra={20:'20 0 R'}),True)
        packed_documents=[]
        for resources,value in (('<< /ColorSpace << /DefaultRGB 20 0 R >> >>',RGB),
                                ('<< /ColorSpace 20 0 R >>','<< /DefaultRGB '+RGB+' >>'),
                                ('20 0 R','<< /ColorSpace << /DefaultRGB '+RGB+' >> >>')):
            pdf=PDF()
            pdf.obj(1,b'<< /Type /Catalog /Pages 2 0 R >>')
            pdf.obj(2,b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>')
            pdf.obj(3,('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Resources '+resources+' >>').encode())
            pdf.obj(4,stream(raw))
            pdf.obj(15,stream(zlib.compress(b'20 0 '+value.encode()),'/Type /ObjStm /N 1 /First 5 /Filter /FlateDecode'))
            pdf.xref(30,entries={0:(0,0,65535),**pdf.objects,20:(2,15,0)})
            packed_documents.append(bytes(pdf.data))
            same(packed_documents[-1],fixture(rgb))
        for pdf in (*packed_documents,fixture(raw),fixture(raw,default_rgb='13 0 R',extra={13:RGB}),fixture(raw,default_rgb='[/ICCBased 10 0 R]',extra=objects)):
            path.write_bytes(pdf)
            run=subprocess.run([allocator,str(path),'0'],capture_output=True,text=True,timeout=30)
            assert run.returncode==0,run.stdout+run.stderr
            allocations+=int(run.stdout.split(': ')[1].split(' checks')[0])
    print(f'PDF default colors: {checks} checks; {references} independent comparisons; {allocations} allocation checks')


if __name__=='__main__':
    main()
