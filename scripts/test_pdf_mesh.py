#!/usr/bin/env python3
"""Exercise byte-aligned triangle meshes and exact barycentric color samples."""
from pathlib import Path
import math
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm

VERTICES=[(0,0,0,(0,0,0)),(0,1,0,(1,0,0)),(0,0,1,(0,1,0)),(1,1,1,(1,1,0))]


def records(vertices,kind=4,coordinate=8,component=8,flag=8,pad=0):
    output=bytearray()
    for f,x,y,values in vertices:
        fields=([(f,flag)] if kind==4 else [])+[(round(x*((1<<coordinate)-1)),coordinate),(round(y*((1<<coordinate)-1)),coordinate)]+[(round(v*((1<<component)-1)),component) for v in values]
        bits=''.join(format(value,'0'+str(width)+'b')[-width:] for value,width in fields)
        bits+=str(pad)*((-len(bits))%8)
        output+=int(bits,2).to_bytes(len(bits)//8,'big')
    return bytes(output)


def fixture(vertices=VERTICES,kind=4,coordinate=8,component=8,flag=8,space='/DeviceRGB',decode=None,function=None,columns=2,attrs='',content='/Sh sh',compressed=False,pad=0,raw=None,extra=None,resources=''):
    values=len(vertices[0][3]) if vertices else (1 if function else 3)
    decode=decode or [0,18,0,12]+[v for _ in range(values) for v in (0,1)]
    data=records(vertices,kind,coordinate,component,flag,pad) if raw is None else raw
    if compressed:data=zlib.compress(data)
    fields=f'/ShadingType {kind} /ColorSpace {space} /BitsPerCoordinate {coordinate} /BitsPerComponent {component} '+(f'/BitsPerFlag {flag}' if kind==4 else f'/VerticesPerRow {columns}')+' /Decode ['+' '.join(map(str,decode))+'] '+attrs
    objects={}
    if function:fields+=' /Function 8 0 R';objects[8]=function
    if compressed:fields+=' /Filter /FlateDecode'
    objects[5]=stream(data,fields)
    objects.update(extra or {})
    return document('/MediaBox [0 0 18 12] /Resources << /Shading << /Sh 5 0 R >> /ExtGState << /Half << /ca .5 >> >> '+resources+' >>',stream=content,extra=objects)


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=refs=0
    white=(255,255,255);red=(255,0,0)
    gradient=lambda x,y:(math.floor(255*(x+.5)/24+.5),math.floor(255*(1-(y+.5)/16)+.5),0)
    with tempfile.TemporaryDirectory(prefix='cupidimage-mesh-') as tmp:
        path=Path(tmp)/'case.pdf'
        def check(name,pdf,expected=None,reject=False,reference=True,secondary=False):
            nonlocal checks,refs
            path.write_bytes(pdf);result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert result.returncode==(3 if reject else 0),(name,result.returncode,result.stderr)
            checks+=1
            if reject:return
            assert struct.unpack('=III',result.stdout[:12])==(24,16,1)
            want=bytes(v for y in range(16) for x in range(24) for v in (*expected(x,y),255))
            assert result.stdout[12:]==want,(name,'analytic differs',next((i,a,b) for i,(a,b) in enumerate(zip(result.stdout[12:],want)) if a!=b))
            if '--reference' in sys.argv and reference:
                if secondary:
                    import fitz
                    fitz.TOOLS.set_aa_level(0)
                    with fitz.open(path) as ref:
                        pix=ref[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                        w,h,rgb=pix.width,pix.height,pix.samples
                else:
                    prefix=Path(tmp)/'reference';run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                    assert run.returncode==0,run.stderr;w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                actual=bytes(v for i,v in enumerate(result.stdout[12:]) if i%4!=3)
                assert (w,h)==(24,16) and max(abs(a-b) for a,b in zip(actual,rgb))<=1,(name,'independent differs',max(abs(a-b) for a,b in zip(actual,rgb)))
                refs+=1
        check('free-form-gradient',fixture(),gradient,reference=False)
        check('lattice-gradient',fixture(kind=5),gradient,reference=False)
        check('compressed',fixture(compressed=True),gradient,reference=False)
        check('clip',fixture(content='0 0 9 12 re W n /Sh sh'),lambda x,y:gradient(x,y) if x<12 else white,reference=False)
        check('bbox',fixture(attrs='/BBox [0 0 9 12]'),lambda x,y:gradient(x,y) if x<12 else white,reference=False)
        # Opacity acts on the interpolated components, not their byte representation.
        check('opacity-once',fixture(content='/Half gs /Sh sh'),lambda x,y:tuple(math.floor(c*.5+128) for c in (255*(x+.5)/24,255*(1-(y+.5)/16),0)),reference=False)
        alt=[VERTICES[0],VERTICES[1],VERTICES[3],(2,0,1,(0,1,0))]
        check('flag-two',fixture(vertices=alt),gradient,reference=False)
        ignored=[VERTICES[0],(3,*VERTICES[1][1:]),(3,*VERTICES[2][1:]),VERTICES[3]]
        check('ignored-new-triangle-flags',fixture(vertices=ignored),gradient,reference=False)
        scalar=[(f,x,y,(x,)) for f,x,y,_ in VERTICES]
        function='<< /FunctionType 2 /Domain [0 1] /C0 [0 0 0] /C1 [1 0 0] /N 2 >>'
        check('function-after-interpolation',fixture(vertices=scalar,function=function),lambda x,y:(math.floor(255*((x+.5)/24)**2+.5),0,0),reference=False)
        sampled=stream(bytes([0,0,0,255,0,0]),'/FunctionType 0 /Domain [0 1] /Range [0 1 0 1 0 1] /Size [2] /BitsPerSample 8')
        check('sampled-function',fixture(vertices=scalar,function=sampled),lambda x,y:(gradient(x,y)[0],0,0),reference=False)
        stitched='<< /FunctionType 3 /Domain [0 1] /Functions ['+function+'] /Bounds [] /Encode [0 1] >>'
        check('stitched-function',fixture(vertices=scalar,function=stitched),lambda x,y:(math.floor(255*((x+.5)/24)**2+.5),0,0),reference=False)
        calculator=stream('{ dup mul 0 0 }','/FunctionType 4 /Domain [0 1] /Range [0 1 0 1 0 1]')
        check('calculator-function',fixture(vertices=scalar,function=calculator),lambda x,y:(math.floor(255*((x+.5)/24)**2+.5),0,0),reference=False)
        constant=stream('{ pop 1 0 0 }','/FunctionType 4 /Domain [0 1] /Range [0 1 0 1 0 1]')
        check('constant-calculator',fixture(vertices=scalar,function=constant),lambda x,y:red)
        components='[<< /FunctionType 2 /Domain [0 1] /N 1 >> << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >> << /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [0] /N 1 >>]'
        check('component-functions',fixture(vertices=scalar,function=components),lambda x,y:(gradient(x,y)[0],math.floor(255*(1-(x+.5)/24)+.5),0),reference=False)
        check('lattice-function',fixture(vertices=scalar,kind=5,function=function),lambda x,y:(math.floor(255*((x+.5)/24)**2+.5),0,0),reference=False)
        check('reversed-function-decode',fixture(vertices=scalar,function=function,decode=[0,18,0,12,1,0]),lambda x,y:(math.floor(255*(1-(x+.5)/24)**2+.5),0,0),reference=False)
        for kind in (4,5):
            check('low-contrast-'+str(kind),fixture(kind=kind,decode=[0,18,0,12,0,16/255,0,16/255,0,16/255]),lambda x,y:(math.floor(16*(x+.5)/24+.5),math.floor(16*(1-(y+.5)/16)+.5),0),reference=False)
        for kind in (4,5):
            for coordinate in (1,2,4,8,12,16,24,32):
                for component in (1,2,4,8,12,16):
                    solid=[(f,x,y,(1,0,0)) for f,x,y,_ in VERTICES]
                    check(f'bits-{kind}-{coordinate}-{component}',fixture(vertices=solid,kind=kind,coordinate=coordinate,component=component,flag=2,pad=1),lambda x,y:red)
        for flag in (2,4,8):
            solid=[(f|(((1<<flag)-1)&~3),x,y,(1,0,0)) for f,x,y,_ in VERTICES]
            check('flag-width-'+str(flag),fixture(vertices=solid,flag=flag),lambda x,y:red,reference=flag==2)
        solid=[(f,x,y,(1,0,0)) for f,x,y,_ in VERTICES]
        for flag in (4,8):
            check('low-flag-bits-'+str(flag),fixture(vertices=solid,flag=flag),lambda x,y:red)
        check('translated',fixture(content='1 0 0 1 6 0 cm /Sh sh'),lambda x,y:(math.floor(255*(x+.5-8)/24+.5),gradient(x,y)[1],0) if x>=8 else white,reference=False)
        check('scaled',fixture(content='.5 0 0 1 0 0 cm /Sh sh'),lambda x,y:(math.floor(255*(x+.5)/12+.5),gradient(x,y)[1],0) if x<12 else white,reference=False)
        check('mirrored-bbox',fixture(content='-1 0 0 1 18 0 cm /Sh sh',attrs='/BBox [0 0 9 12]'),lambda x,y:(math.floor(255*(1-(x+.5)/24)+.5),gradient(x,y)[1],0) if x>=12 else white,reference=False)
        check('reversed-decode',fixture(decode=[18,0,0,12,0,1,0,1,0,1]),lambda x,y:(math.floor(255*(1-(x+.5)/24)+.5),gradient(x,y)[1],0),reference=False)
        gray=[(f,x,y,(x,)) for f,x,y,_ in VERTICES]
        check('gray',fixture(vertices=gray,space='/DeviceGray'),lambda x,y:(gradient(x,y)[0],)*3,reference=False)
        cmyk=[(f,x,y,(0,1-x,1,0)) for f,x,y,_ in VERTICES]
        check('cmyk',fixture(vertices=cmyk,space='/DeviceCMYK'),lambda x,y:(255,gradient(x,y)[0],0),reference=False)
        check('named-color',fixture(vertices=solid,space='/CS1',resources='/ColorSpace << /CS1 /DeviceRGB >>'),reject=True)
        check('array-color',fixture(vertices=solid,space='[/DeviceRGB]'),lambda x,y:red)
        check('indirect-color',fixture(vertices=solid,space='9 0 R',extra={9:b'/DeviceRGB'}),lambda x,y:red)
        check('null-function',fixture(vertices=solid,attrs='/Function null'),lambda x,y:red)
        check('null-bbox',fixture(vertices=solid,attrs='/BBox null'),lambda x,y:red)
        check('indirect-null-function',fixture(vertices=solid,function='null'),lambda x,y:red)
        check('indexed-base-interpolation',fixture(vertices=gray,space='[/Indexed /DeviceRGB 1 <ff00000000ff>]'),lambda x,y:(math.floor(255*(1-(x+.5)/24)+.5),0,gradient(x,y)[0]),reference=False)
        lattice=[(0,x/3,y/2,(x/3,y/2,0)) for y in range(3) for x in range(4)]
        check('lattice-rows',fixture(vertices=lattice,kind=5,columns=4),gradient,reference=False)
        overlap=solid+[(f,x,y,(0,0,1)) for f,x,y,_ in VERTICES]
        check('overlap-one-object',fixture(vertices=overlap,content='/Half gs /Sh sh'),lambda x,y:(128,128,255),secondary=True)
        pattern=b'<< /PatternType 2 /Shading 5 0 R >>'
        check('pattern',fixture(vertices=solid,content='/Pattern cs /P scn 0 0 18 12 re f',extra={7:pattern},resources='/Pattern << /P 7 0 R >>'),lambda x,y:red)
        check('pattern-opacity-once',fixture(vertices=solid,content='/Half gs /Pattern cs /P scn 0 0 18 12 re f',extra={7:pattern},resources='/Pattern << /P 7 0 R >>'),lambda x,y:(255,128,128),secondary=True)
        check('null-pattern-background',fixture(vertices=solid,attrs='/Background null',content='/Pattern cs /P scn 0 0 18 12 re f',extra={7:pattern},resources='/Pattern << /P 7 0 R >>'),lambda x,y:red)
        covered=lambda x,y:(x+.5)/24+1-(y+.5)/16<1
        check('pattern-background-outside',fixture(vertices=solid[:3],attrs='/Background [0 0 1]',content='/Pattern cs /P scn 0 0 18 12 re f',extra={7:pattern},resources='/Pattern << /P 7 0 R >>'),lambda x,y:red if covered(x,y) else (0,0,255),reference=False)
        check('direct-background-outside-ignored',fixture(vertices=solid[:3],attrs='/Background [0 0 1]'),lambda x,y:red if covered(x,y) else white,reference=False)
        check('direct-background-ignored',fixture(vertices=solid,attrs='/Background [0 0 1]'),lambda x,y:red)
        check('empty',fixture(vertices=[]),lambda x,y:white,reference=False)
        check('one-lattice-row',fixture(vertices=solid[:2],kind=5),lambda x,y:white,reference=False)
        check('singular-matrix',fixture(vertices=solid,content='0 0 0 1 0 0 cm /Sh sh'),lambda x,y:white,reference=False)
        check('degenerate',fixture(vertices=[(0,0,0,(1,0,0))]*3),lambda x,y:white,reference=False)
        for attrs in ('/Decode [0 18 0 12 0 1]','/BitsPerCoordinate 3'):
            # Replace the whole object to avoid duplicate dictionary keys.
            data=stream(records(solid),'/ShadingType 4 /ColorSpace /DeviceRGB /BitsPerComponent 8 /BitsPerFlag 8 '+attrs)
            check('missing-fields',fixture(extra={5:data}),reject=True)
        check('function-domain',fixture(vertices=scalar,function=function,decode=[0,18,0,12,0,2]),reject=True)
        check('indexed-function',fixture(vertices=gray,space='[/Indexed /DeviceRGB 1 <ff00000000ff>]',function=function),reject=True)
        for kwargs in ({'coordinate':3},{'component':3},{'flag':1},{'raw':records(VERTICES)[:-1]},{'vertices':VERTICES[:2]},{'vertices':[(1,*VERTICES[0][1:])]},{'vertices':VERTICES+[(3,0,0,(0,0,0))]},{'kind':5,'columns':1},{'kind':5,'vertices':VERTICES[:3]}):
            check('invalid-'+str(kwargs),fixture(**kwargs),reject=True)
    print(f'PDF triangle meshes: {checks} checks passed; {refs} independent comparisons')


if __name__=='__main__':main()
