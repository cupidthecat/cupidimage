#!/usr/bin/env python3
"""Exercise bounded multi-component tint sources across PDF painting paths."""
from pathlib import Path
import math
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_type3 import stream
from test_pdf_embedded_fonts import fixture as text_fixture
from test_pdf_mesh import fixture as mesh_fixture, VERTICES
from test_pdf_patch import fixture as patch_fixture, control_points
from audit_pdf_corpus import read_ppm


def names(n):return ' '.join('/Ink'+str(i) for i in range(n))
def space(n,attributes='',colorants=None):
    return '[/DeviceN ['+(names(n) if colorants is None else colorants)+'] /DeviceRGB 10 0 R '+attributes+']'

def function(n,sampled=False):
    if sampled:
        axes=min(n,3);data=bytearray()
        for corner in range(1<<axes):
            value=[(corner>>axis)&1 for axis in range(axes)]
            data.extend([255*(1-value[0])]*3 if n==1 else [255*(1-value[0]),255*(1-value[1]),255*(1-value[2]) if n>=3 else 0])
        return stream(data,'/FunctionType 0 /Domain ['+'0 1 '*n+'] /Range [0 1 0 1 0 1] /Size ['+'2 '*axes+'1 '*(n-axes)+'] /BitsPerSample 8')
    program=('pop '*(n-3) if n>=3 else 'dup dup ' if n==1 else '')
    program+=('1 exch sub 3 1 roll '*3) if n!=2 else '1 exch sub exch 1 exch sub exch 0'
    return stream('{ '+program+' }','/FunctionType 4 /Domain ['+'0 1 '*n+'] /Range [0 1 0 1 0 1]')

def fixture(content,n=3,cs=None,extra=None,resources='',spaces='',sampled=False):
    objects={10:function(n,sampled)};objects.update(extra or {})
    return document('/MediaBox [0 0 18 12] /Resources << /ColorSpace << /S '+(cs or space(n))+' '+spaces+' >> '+resources+' >>',stream=content,extra=objects)

def rgb(values):
    values=[max(0,min(1,v)) for v in values]
    return [1-values[0]]*3 if len(values)==1 else [1-values[0],1-values[1],1-values[2] if len(values)>=3 else 0]

def paint(values):return ' '.join(str(v) for v in values)+' rg 0 0 18 12 re f'


def main():
    probe,allocator=[str(Path(arg).resolve()) for arg in sys.argv[1:3]]
    checks=references=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-devicen-') as directory:
        root=Path(directory);path=root/'case.pdf'
        def render(pdf,reject=False):
            nonlocal checks
            path.write_bytes(pdf);run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=25)
            assert run.returncode==(3 if reject else 0),run.stderr
            checks+=1
            return run.stdout
        def same(pdf,expected,reference=False):
            nonlocal references
            actual=render(pdf);assert actual==render(expected),'multi-component pixels differ'
            if reference and '--reference' in sys.argv:
                path.write_bytes(pdf);prefix=root/'reference'
                run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=25)
                assert run.returncode==0,run.stderr
                w,h,data=read_ppm(prefix.with_suffix('.ppm'))
                assert (w,h)==(24,16) and max(abs(data[i]-actual[12+(i//3)*4+i%3]) for i in range(len(data)))<=1
                references+=1
        def fault(pdf):
            nonlocal allocations
            path.write_bytes(pdf);run=subprocess.run([allocator,str(path),'0'],capture_output=True,text=True,timeout=40)
            assert run.returncode==0,run.stdout+run.stderr
            allocations+=int(run.stdout.split(': ')[1].split(' checks')[0])
        for n in (1,2,3,4,8,16,32):
            values=([.25,.5,.75]*11)[:n];text=' '.join(map(str,values));expected=fixture(paint(rgb(values)))
            for sampled in (False,True):
                pdf=fixture('/S cs '+text+' scn 0 0 18 12 re f',n=n,sampled=sampled)
                same(pdf,expected,not sampled or n<=16)
            same(fixture('/S CS '+text+' SCN 12 w 0 6 m 18 6 l S',n=n),expected)
            same(fixture('/S cs 0 0 18 12 re f',n=n),fixture(paint(rgb([1]*n))))
            same(fixture('/S cs '+text+' scn q 0 g Q 0 0 18 12 re f',n=n),expected)
            fault(fixture('/S cs '+text+' scn 0 0 18 12 re f',n=n))
        # Multilinear interpolation must use every active axis, not just the first two.
        for n in (4,8,16):
            data=bytearray()
            for corner in range(1<<n):
                data.extend((255 if corner==(1<<n)-1 else 0,255 if corner==0 else 0,255 if bin(corner).count('1')%2 else 0))
            sampled=stream(data,'/FunctionType 0 /Domain ['+'0 1 '*n+'] /Range [0 1 0 1 0 1] /Size ['+'2 '*n+'] /BitsPerSample 8')
            pdf=document('/MediaBox [0 0 .75 .75] /Resources << /ColorSpace << /S '+space(n)+' >> >>',stream='/S cs '+'.25 '*n+'scn 0 0 .75 .75 re f',extra={10:sampled})
            actual=render(pdf)
            expected=(.25**n,.75**n,.5*(1-.5**n))
            assert actual[12:]==bytes([math.floor(v*255+.5) for v in expected]+[255]),n
        for n in (2,8,32):
            samples=bytes(([64,128,192]*11)[:n]);values=[v/255 for v in samples];expected=fixture(paint(rgb(values)))
            image_attrs='/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace '+space(n)
            image=stream(samples,image_attrs)
            same(fixture('18 0 0 12 0 0 cm /Im Do',n=n,extra={11:image},resources='/XObject << /Im 11 0 R >>'),expected,True)
            same(fixture('18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /S /F /AHx ID '+samples.hex()+'> EI',n=n),expected,True)
            same(fixture('/I cs 1 scn 0 0 18 12 re f',n=n,spaces='/I [/Indexed '+space(n)+' 1 <'+(bytes(n)+samples).hex()+'>]'),expected,True)
            mask=' /Mask ['+' '.join(str(v) for byte in samples for v in (byte,byte))+']'
            same(fixture('18 0 0 12 0 0 cm /Im Do',n=n,extra={11:stream(samples,image_attrs+mask)},resources='/XObject << /Im 11 0 R >>'),fixture('q Q'))
            scalar=[(f,x,y,tuple(values)) for f,x,y,_ in VERTICES]
            target=[(f,x,y,tuple(rgb(values))) for f,x,y,_ in VERTICES]
            same(mesh_fixture(vertices=scalar,space=space(n),extra={10:function(n)}),mesh_fixture(vertices=target))
            for kind in (6,7):
                same(patch_fixture(kind=kind,patches=[(0,control_points(kind),[tuple(values)]*4)],space=space(n),extra={10:function(n)}),
                     patch_fixture(kind=kind,patches=[(0,control_points(kind),[tuple(rgb(values))]*4)]))
            cell=stream('0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>')
            same(fixture('/P cs '+' '.join(map(str,values))+' /Tile scn 0 0 18 12 re f',n=n,spaces='/P [/Pattern '+space(n)+']',extra={11:cell},resources='/Pattern << /Tile 11 0 R >>'),expected)
        n=32;values=[64/255,128/255,192/255]+[0]*29
        values_text=' '.join(map(str,values));expected=fixture(paint(rgb(values)))
        shade='<< /ShadingType 2 /ColorSpace '+space(n)+' /Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] /C0 ['+values_text+'] /C1 ['+values_text+'] /N 1 >> /Extend [true true] >>'
        same(fixture('/Sh sh',n=n,extra={11:shade},resources='/Shading << /Sh 11 0 R >>'),expected)
        out_function=stream(bytes([64,128,192]+[0]*29)*2,'/FunctionType 0 /Domain [0 1] /Range ['+'0 1 '*n+'] /Size [2] /BitsPerSample 8')
        shade='<< /ShadingType 2 /ColorSpace '+space(n)+' /Coords [0 0 18 0] /Function 12 0 R /Extend [true true] >>'
        same(fixture('/Sh sh',n=n,extra={11:shade,12:out_function},resources='/Shading << /Sh 11 0 R >>'),expected)
        image_attrs='/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 1 /ColorSpace '+space(n)
        same(fixture('18 0 0 12 0 0 cm /Im Do',n=n,extra={11:stream(bytes([0x55])*4,image_attrs)},resources='/XObject << /Im 11 0 R >>'),fixture(paint([1,0,1])))
        same(fixture('18 0 0 12 0 0 cm /Im Do',n=n,extra={11:stream(bytes([0x55])*4,image_attrs+' /Decode ['+'1 0 '*n+']')},resources='/XObject << /Im 11 0 R >>'),fixture(paint([0,1,0])))
        mask=stream(bytes([128]),'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray /Matte ['+'0 '*n+']')
        attrs='/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace '+space(n)+' /SMask 12 0 R'
        masked=fixture('18 0 0 12 0 0 cm /Im Do',n=n,extra={11:stream(bytes([128])*n,attrs),12:mask},resources='/XObject << /Im 11 0 R >>')
        same(masked,fixture(paint([127/255]*3)));fault(masked)
        # Wide palette storage is shared without merging its base-space semantics.
        indexed='/I [/Indexed '+space(n)+' 1 <'+(bytes(n)+bytes([64,128,192]+[0]*29)).hex()+'>]'
        palette_pdf=fixture('/I cs 1 scn 0 0 18 12 re f',n=n,spaces=indexed)
        same(palette_pdf,expected);fault(palette_pdf)
        same(fixture(('/I cs '*1000)+'1 scn 0 0 18 12 re f',n=n,spaces=indexed),expected)
        for count,reject in ((256,False),(257,True)):
            spaces=' '.join('/I'+str(i)+' [/Indexed '+space(2)+' 0 <'+i.to_bytes(2,'big').hex()+'>]' for i in range(count))
            render(fixture(' '.join('/I'+str(i)+' cs' for i in range(count)),n=2,spaces=spaces),reject)
        for n in (3,4):
            values=[.25,.5,.75,.25][:n];device='DeviceRGB' if n==3 else 'DeviceCMYK'
            same(fixture('/'+device+' cs '+' '.join(map(str,values))+' scn 0 0 18 12 re f',n=n,spaces='/Default'+device[6:]+' '+space(n)),fixture(paint(rgb(values))))
        left=bytes([64,128,192]+[0]*29);right=bytes([255])*32
        predicted=left+bytes((b-a)%256 for a,b in zip(left,right))
        attrs='/Type /XObject /Subtype /Image /Width 2 /Height 1 /BitsPerComponent 8 /ColorSpace '+space(32)+' /Filter /FlateDecode /DecodeParms << /Predictor 2 /Colors 32 /Columns 2 /BitsPerComponent 8 >>'
        predicted_pdf=fixture('18 0 0 12 0 0 cm /Im Do',n=32,extra={11:stream(zlib.compress(predicted),attrs)},resources='/XObject << /Im 11 0 R >>')
        same(predicted_pdf,fixture('0 0 0 rg 0 0 18 12 re f '+paint([191/255,127/255,63/255]).replace('18 12 re','9 12 re')));fault(predicted_pdf)
        for colorants in ('/None /None','/Ink0 /None'):
            pdf=fixture('/S cs .25 .75 scn 0 0 18 12 re f',n=2,cs=space(2,colorants=colorants))
            same(pdf,fixture('q Q' if colorants.startswith('/None') else paint([.75,.25,0])),True)
        for attributes in ('','<< >>','<< /Subtype /DeviceN >>','null','11 0 R'):
            same(fixture('/S cs .25 .75 scn 0 0 18 12 re f',n=2,cs=space(2,attributes),extra={11:'<< /Subtype /DeviceN >>'}),fixture(paint([.75,.25,0])))
        for cs,n in ((space(0),1),(space(33),1),(space(2,colorants='/A /A'),2),(space(1,colorants='/All'),1),
                     (space(2,'<< /Subtype /NChannel >>'),2),(space(2,'42'),2)):
            render(fixture('/S cs 0 0 18 12 re f',n=n,cs=cs),True)
        for count in (31,33):
            render(fixture('/S cs '+'0 '*count+'scn 0 0 18 12 re f',n=32),True)
        # Indexed patch corners expand through the base component ranges.
        lab='[/Lab << /WhitePoint [.95047 1 1.08883] /Range [-128 127 -128 127] >>]'
        for kind in (6,7):
            same(patch_fixture(kind=kind,patches=[(0,control_points(kind),[(0,)]*4)],space='[/Indexed '+lab+' 0 <808080>]'),
                 patch_fixture(kind=kind,patches=[(0,control_points(kind),[(128/255,)*3]*4)],space=lab,decode=[0,18,0,12,0,100,-128,127,-128,127]))
        for n in (2,32):
            values=[.25]*n;page={3:'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 120 90] /Contents 4 0 R /Resources << /Font << /F 5 0 R >> /ColorSpace << /S '+space(n)+' >> >> >>',10:function(n)}
            same(text_fixture('/S cs '+' '.join(map(str,values))+' scn BT /F 30 Tf 20 30 Td (A) Tj ET',extra=page),
                 text_fixture(' '.join(map(str,rgb(values)))+' rg 20 30 12 18 re f'))
    print(f'PDF DeviceN colors: {checks} checks; {references} independent comparisons; {allocations} allocation checks')


if __name__=='__main__':main()
