#!/usr/bin/env python3
"""Exercise process-only NChannel colors independently of aggregate tint functions."""
import itertools
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_type3 import stream
from test_pdf_calibrated import RGB, GRAY
from test_pdf_embedded_fonts import fixture as text_fixture
from test_pdf_mesh import fixture as mesh_fixture, VERTICES
from test_pdf_patch import fixture as patch_fixture, control_points
from audit_pdf_corpus import read_ppm
from test_pdf_icc import profile, gamma


def space(names=('Red','Green','Blue'), process='/DeviceRGB', components=None,
          alternate='/DeviceRGB', attributes=''):
    components = components or names
    return ('[/DeviceN ['+' '.join('/'+n for n in names)+'] '+alternate+
            ' 10 0 R << /Subtype /NChannel /Process << /ColorSpace '+process+
            ' /Components ['+' '.join('/'+n for n in components)+'] >> '+attributes+' >>]')


def function(inputs=3, outputs=3, identity=False):
    program = '' if identity else 'pop '*inputs+'0 '*outputs
    return stream('{ '+program+' }','/FunctionType 4 /Domain ['+'0 1 '*inputs+'] /Range ['+'0 1 '*outputs+']')


def fixture(content, cs=None, inputs=3, outputs=3, identity=False, extra=None, resources='', spaces=''):
    objects={10:function(inputs, outputs, identity)};objects.update(extra or {})
    return document('/MediaBox [0 0 18 12] /Resources << /ColorSpace << /S '+(cs or space())+' '+spaces+' >> '+resources+' >>', stream=content, extra=objects)


def paint(values):
    return '/S cs '+' '.join(map(str,values))+' scn 0 0 18 12 re f'


def direct(values, cs='/DeviceRGB'):
    return fixture(paint(values), cs=cs)


def main():
    probe, allocator = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    checks=references=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-nchannel-') as tmp:
        path=Path(tmp)/'case.pdf'
        def render(data,reject=False):
            nonlocal checks
            path.write_bytes(data)
            result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=30)
            assert (result.returncode!=0)==reject,result.stderr
            checks+=1
            return result.stdout
        def same(data,expected,reference=False):
            nonlocal references
            actual=render(data);assert actual==render(expected),'source colors differ'
            if reference and '--reference' in sys.argv:
                path.write_bytes(data);prefix=Path(tmp)/'reference'
                result=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=30)
                assert result.returncode==0,result.stderr
                w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                pixels=bytes(v for i,v in enumerate(actual[12:]) if i%4!=3)
                assert (w,h)==(24,16) and max(abs(a-b) for a,b in zip(pixels,rgb))<=1,'reference differs'
                references+=1
        def fault(data):
            nonlocal allocations
            path.write_bytes(data)
            result=subprocess.run([allocator,str(path),'0'],capture_output=True,text=True,timeout=120)
            assert result.returncode==0,(result.stdout,result.stderr)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        for names in (('Red','Green','Blue'),('R','G','B'),('','x','y')):
            for process in ('/DeviceRGB',RGB):
                cs=space(names,process,alternate=process)
                for values in ([0,0,0],[1,1,1],[.25,.5,.75],[-1,2,.5]):
                    # The deliberately black aggregate alternate must not replace the process.
                    same(fixture(paint(values),cs),direct(values,process))
                same(fixture(paint([.25,.5,.75]),cs,identity=True),direct([.25,.5,.75],process),True)
                same(fixture('0 g 0 0 18 12 re f /S cs 0 0 18 12 re f',cs),direct([1,1,1],process))
        for process in ('/DeviceGray',GRAY):
            cs=space(('Tone',),process,alternate=process)
            for value in (0,.25,.5,1):
                same(fixture(paint([value]),cs,inputs=1,outputs=1,identity=True),direct([value],process),True)
        cmyk=('Cyan','Magenta','Yellow','Black');values=(.25,.5,.75,.125)
        for count in range(1,5):
            for names in itertools.permutations(cmyk,count):
                selected=[values[cmyk.index(name)] for name in names]
                full=[values[i] if name in names else 0 for i,name in enumerate(cmyk)]
                cs=space(names,'/DeviceCMYK',cmyk)
                same(fixture(paint(selected),cs,inputs=count),direct(full,'/DeviceCMYK'))
        cs=space(('Yellow','Cyan'),'/DeviceCMYK',('c','m','y','k'))
        same(fixture(paint([.75,.25]),cs,inputs=2),direct([.25,0,.75,0],'/DeviceCMYK'))
        # Definitions for process colorants are ignored, even if they disagree.
        cs=space(attributes='/Colorants << /Red [/Separation /Red /DeviceGray 10 0 R] >>')
        same(fixture(paint([1,0,0]),cs),direct([1,0,0]))
        ignored='[/ICCBased 11 0 R]'
        cs=space(alternate=ignored)
        same(fixture(paint([.25,.5,.75]),cs,extra={11:stream('unsupported profile','/N 3'),10:stream('{ unknownOperator }','/FunctionType 4 /Domain [0 1 0 1 0 1] /Range [0 1 0 1 0 1]')}),direct([.25,.5,.75]))
        rgba=[64/255,128/255,192/255]
        for cs in (space(),space(('Yellow','Cyan'),'/DeviceCMYK',cmyk)):
            n=3 if cs==space() else 2;pixel=bytes([64,128,192][:n]);expected=rgba if n==3 else [128/255,0,64/255,0]
            attrs='/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace '+cs
            data=fixture('18 0 0 12 0 0 cm /Im Do',cs,inputs=n,extra={11:stream(pixel,attrs)},resources='/XObject << /Im 11 0 R >>')
            same(data,direct(expected,'/DeviceRGB' if n==3 else '/DeviceCMYK'));fault(data)
        cs=space();values=[64/255,128/255,192/255]
        inline=fixture('18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /S ID abc EI').replace(b'abc',bytes([64,128,192]))
        same(inline,direct(values));fault(inline)
        same(fixture('/I cs 1 scn 0 0 18 12 re f',spaces='/I [/Indexed '+cs+' 1 <0000004080c0>]'),direct(values))
        cell=stream('0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>')
        data=fixture('/P cs .25 .5 .75 /Tile scn 0 0 18 12 re f',spaces='/P [/Pattern '+cs+']',extra={11:cell},resources='/Pattern << /Tile 11 0 R >>')
        same(data,direct([.25,.5,.75]));fault(data)
        vertices=[(f,x,y,tuple(values)) for f,x,y,_ in VERTICES]
        same(mesh_fixture(vertices=vertices,space=cs,extra={10:function()}),mesh_fixture(vertices=vertices))
        for kind in (6,7):
            patches=[(0,control_points(kind),[tuple(values)]*4)]
            same(patch_fixture(kind=kind,patches=patches,space=cs,extra={10:function()}),patch_fixture(kind=kind,patches=patches))
        shade='<< /ShadingType 2 /ColorSpace '+cs+' /Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [.25 .5 .75] /C1 [.25 .5 .75] /N 1 >> /Extend [true true] >>'
        data=fixture('/Sh sh',extra={11:shade},resources='/Shading << /Sh 11 0 R >>')
        same(data,direct([.25,.5,.75]));fault(data)
        page={3:'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 120 90] /Contents 4 0 R /Resources << /Font << /F 5 0 R >> /ColorSpace << /S '+cs+' >> >> >>',10:function()}
        data=text_fixture('/S cs .25 .5 .75 scn BT /F 30 Tf 20 30 Td (A) Tj ET',extra=page)
        same(data,text_fixture('.25 .5 .75 rg 20 30 12 18 re f'));fault(data)
        # Process and component metadata can be indirect, including individual names.
        cs='[/DeviceN 11 0 R /DeviceRGB 10 0 R 12 0 R]'
        data=fixture(paint([.25,.5,.75]),cs,extra={11:'[15 0 R 16 0 R 17 0 R]',12:'<< /Subtype 18 0 R /Process 13 0 R >>',13:'<< /ColorSpace /DeviceRGB /Components 14 0 R >>',14:'[15 0 R 16 0 R 17 0 R]',15:'/Red',16:'/Green',17:'/Blue',18:'/NChannel'})
        same(data,direct([.25,.5,.75]));fault(data)
        for gray in (False,True):
            count=1 if gray else 3
            process='[/ICCBased 11 0 R]'
            cs=space(('Tone',) if gray else ('Red','Green','Blue'),process)
            extra={11:stream(profile(curves=[gamma(2)]*count,gray=gray),'/N '+str(count))}
            data=fixture(paint([.5]*count),cs,inputs=count,extra=extra)
            same(data,fixture(paint([.5]*count),cs=process,extra=extra));fault(data)
        mask=stream(bytes([128]),'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray /Matte [0 0 0]')
        attrs='/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /SMask 12 0 R /ColorSpace '
        data=fixture('18 0 0 12 0 0 cm /Im Do',extra={11:stream(bytes([64,96,128]),attrs+space()),12:mask},resources='/XObject << /Im 11 0 R >>')
        expected=fixture('18 0 0 12 0 0 cm /Im Do',extra={11:stream(bytes([64,96,128]),attrs+'/DeviceRGB'),12:mask},resources='/XObject << /Im 11 0 R >>')
        same(data,expected);fault(data)
        same(fixture('/S cs '*1000+paint([.25,.5,.75])),direct([.25,.5,.75]))
        for count,reject in ((256,False),(257,True)):
            spaces=' '.join('/N'+str(i)+' '+space(attributes='/Case '+str(i)) for i in range(count))
            render(fixture(' '.join('/N'+str(i)+' cs' for i in range(count)),spaces=spaces),reject)
        data=fixture('/S cs .25 .5 .75 scn q /S CS .75 .5 .25 SCN 2 w 1 1 16 10 re B Q')
        same(data,fixture('/DeviceRGB cs .25 .5 .75 scn q /DeviceRGB CS .75 .5 .25 SCN 2 w 1 1 16 10 re B Q'));fault(data)
        for bad in (
            space(('None',),'/DeviceGray'),space(('Cyan','Green','Blue')),
            space(('Green','Red','Blue'),components=('Red','Green','Blue')),
            space(('Red','Green'),components=('Red','Green','Blue')),
            space(('Red','Green','Spot'),components=('Red','Green','Blue')),
            space(('Cyan','c'),'/DeviceCMYK',('c','m','y','k')),
            space(process='[/Lab << /WhitePoint [.95047 1 1.08883] >>]'),
            space(process='[/Indexed /DeviceRGB 0 <000000>]'),
            space().replace('/Process <<','/ProcessWrong <<'),
            space(attributes='/Colorants 42'),space(components=('Red','None','Blue')),
            space(components=('Red','Red','Blue')),
            space(('Red','Green','Blue','Spot'),components=('Red','Green','Blue'),attributes='/Colorants << /Spot [/Separation /Spot /DeviceGray << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>] >>'),
        ):
            render(fixture('/S cs 0 0 18 12 re f',bad),True)
        for device,cs,values,expected,output in (
            ('DeviceRGB',space(process=RGB),[.25,.5,.75],[.25,.5,.75],RGB),
            ('DeviceGray',space(('Tone',),GRAY),[.5],[.5],GRAY),
            ('DeviceCMYK',space(('Yellow','Cyan','Black','Magenta'),'/DeviceCMYK',cmyk),[.25,.5,.75,.125],[.5,.125,.25,.75],'/DeviceCMYK'),
        ):
            content='/'+device+' cs '+' '.join(map(str,values))+' scn 0 0 18 12 re f'
            data=fixture(content,inputs=len(values),spaces='/Default'+device[6:]+' '+cs)
            same(data,direct(expected,output));fault(data)
        # A shared raw definition must retain the resource-scoped process transform.
        cs=space(process='/DeviceRGB')
        form=stream('/S cs .25 .5 .75 scn 9 0 9 12 re f','/Type /XObject /Subtype /Form /BBox [0 0 18 12] /Resources << /ColorSpace << /S '+cs+' /DefaultRGB '+RGB+' >> >>')
        data=fixture('/S cs .25 .5 .75 scn 0 0 9 12 re f /X Do',cs,extra={12:form},resources='/XObject << /X 12 0 R >>')
        expected=fixture('/DeviceRGB cs .25 .5 .75 scn 0 0 9 12 re f /C cs .25 .5 .75 scn 9 0 9 12 re f',spaces='/C '+RGB)
        same(data,expected);fault(data)
    print(f'PDF process NChannel: {checks} checks; {references} independent comparisons; {allocations} allocation checks')


if __name__=='__main__':main()
