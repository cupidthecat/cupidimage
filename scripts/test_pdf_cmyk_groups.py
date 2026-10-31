#!/usr/bin/env python3
"""Qualify four-component blending, black generation and retained ink values."""
import itertools
import random
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_cie_groups import page,color,FULL
from test_pdf_transparency import MODES,blend
from test_pdf_forms import form
from test_pdf_type3 import stream
from test_pdf_icc_lut import profile,lut as forward_lut
from test_pdf_icc_reverse import lut
from test_pdf_intents import tags
from test_pdf_images import image_stream

DEVICE=('CMYK',4,'/DeviceCMYK',{},False)

def mixed(mode,source,back):
    result=[1-v for v in blend(mode,[1-v for v in source[:3]],[1-v for v in back[:3]])]
    if mode in MODES[12:]:result.append(source[3] if mode=='Luminosity' else back[3])
    else:result.append(1-blend(mode,[1-source[3]],[1-back[3]])[0])
    return result

def rgb(values):
    return tuple(int((1-min(1,max(0,v)+max(0,values[3])))*255+.5) for v in values[:3])

def main():
    probe,allocator,components=[str(Path(v).resolve()) for v in sys.argv[1:4]]
    checks=allocations=component_checks=0
    rng=random.Random(1135)
    vectors=[((.15,.4,.7,.2),(.7,.2,.3,.1)),((0,1,0,0),(1,0,1,.5)),((.25,)*4,(.75,)*4)]
    vectors += [((0,)*4,(1,)*4),((1,)*4,(0,)*4)]
    vectors += [(tuple(rng.random() for _ in range(4)),tuple(rng.random() for _ in range(4))) for _ in range(40)]
    for mode,(source,back) in itertools.product(MODES,vectors):
        result=subprocess.run([components,str(MODES.index(mode)),*map(str,source),*map(str,back)],capture_output=True,text=True,check=True,timeout=30)
        actual=list(map(float,result.stdout.split()))
        assert max(abs(a-b) for a,b in zip(actual,mixed(mode,source,back)))<1e-12,(mode,actual,mixed(mode,source,back))
        component_checks+=1
    with tempfile.TemporaryDirectory(prefix='cupid-cmyk-groups-') as directory:
        path=Path(directory)/'case.pdf'
        def render(data,expected=None,reject=False,aa=False):
            nonlocal checks
            path.write_bytes(data);r=subprocess.run([probe,str(path),'0',*(['--pdf-text-aa'] if aa else [])],capture_output=True,timeout=30)
            assert (r.returncode!=0)==reject,(r.returncode,r.stderr)
            checks+=1
            if reject:return None
            actual=r.stdout[12:]
            if expected is not None:
                wanted=bytes((*expected,255))*(24*16)
                assert max(abs(a-b) for a,b in zip(actual,wanted))<=1,(actual[:4],wanted[:4])
            return actual
        def fault(data,reject=False):
            nonlocal allocations
            path.write_bytes(data);r=subprocess.run([allocator,str(path),str(int(reject))],capture_output=True,text=True,check=True,timeout=120)
            allocations+=int(r.stdout.split('checks')[0].split()[-1])
        colors=[((.15,.4,.7,.2),(.7,.2,.3,.1)),((0,1,0,0),(1,0,1,.5)),((.25,)*4,(.75,)*4)]
        for mode,alpha,(source,back) in itertools.product(MODES,(0,.25,1),colors):
            result=mixed(mode,source,back)
            values=[(1-alpha)*b+alpha*v for b,v in zip(back,result)]
            data=page(DEVICE,color(back)+FULL+' /Half gs '+color(source)+FULL,
                      objects={30:f'<< /ca {alpha} /BM /{mode} >>'})
            render(data,rgb(values))
        source,back=colors[0]
        for isolated,knockout in itertools.product((False,True),(False,True)):
            body=color(source)+FULL
            child=form(body,'/BBox [0 0 18 12] /Group << /S /Transparency /I '+str(isolated).lower()+' /K '+str(knockout).lower()+' /CS /DeviceCMYK >> /Resources << /ColorSpace << /C /DeviceCMYK >> >>')
            data=page(DEVICE,color(back)+FULL+' /Half gs /G Do',objects={20:child},resources='/XObject << /G 20 0 R >>')
            render(data,rgb([(a+b)/2 for a,b in zip(source,back)]));fault(data)
        # Text, glyph objects, combined fill/stroke and images must retain K.
        for aa in (False,True):
            text=color((.1,.2,.3,.4))+'BT /F 9 Tf 1 2 Td (ABC) Tj ET'
            actual=render(page(DEVICE,text),aa=aa)
            expected=render(page(DEVICE,text,group=False),aa=aa)
            assert max(abs(a-b) for a,b in zip(actual,expected))<=1
        for uncolored in (False,True):
            program=('1000 0 0 0 1000 1000 d1 0 0 1000 1000 re f' if uncolored else
                     '1000 0 d0 0 0 0 0 k 0 0 1000 1000 re f /Half gs .2 .4 .6 .8 k 0 0 1000 1000 re f')
            font='<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [.001 0 0 .001 0 0] /FirstChar 65 /LastChar 65 /Widths [1000] /Encoding << /Differences [65 /A] >> /CharProcs << /A 61 0 R >> /Resources << /ExtGState << /Half 30 0 R >> >> >>'
            doc=page(DEVICE,color((.1,.2,.3,.4))+'BT /F3 18 Tf (A) Tj ET',
                     objects={60:font,61:stream(program)},fonts='/F3 60 0 R')
            render(doc,rgb((.1,.2,.3,.4)));fault(doc)
        payload=bytes((51,102,153,85))
        img=image_stream(payload,'/Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceCMYK /BitsPerComponent 8')
        doc=page(DEVICE,'q 18 0 0 12 0 0 cm /Im Do Q',objects={70:img},resources='/XObject << /Im 70 0 R >>')
        render(doc,rgb([v/255 for v in payload]));fault(doc)
        doc=page(DEVICE,'0 0 0 .25 k 0 0 0 .75 K /Half gs 2 w 3 3 12 6 re B')
        render(doc);fault(doc)
        # DeviceGray supplies black ink, independently of RGB black-generation functions.
        render(page(DEVICE,'.5 g '+FULL),(128,128,128))
        # Black-generation and undercolor-removal functions apply at paint/Do.
        functions=' /BG << /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [.25] /N 1 >> /UCR << /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [.5] /N 1 >> '
        states={30:'<<'+functions+'>>'}
        data=page(DEVICE,'/Half gs .2 .4 .6 rg '+FULL,objects=states)
        render(data,(77,128,179));fault(data)
        child=form('/Reset gs .2 .4 .6 rg '+FULL,'/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS /DeviceRGB >> /Resources << /ExtGState << /Reset << /BG2 /Default /UCR2 /Default >> >> >>')
        data=page(DEVICE,'/Half gs /G Do',objects={**states,20:child},resources='/XObject << /G 20 0 R >>')
        render(data,(77,128,179));fault(data)
        render(page(DEVICE,'q /Half gs .2 .4 .6 rg '+FULL+' Q .2 .4 .6 rg '+FULL,objects=states),(51,102,153))
        render(page(DEVICE,'/Half gs /Outer gs .2 .4 .6 rg '+FULL,objects={30:'<<'+functions+'>>',31:'<< /BG /Bad /BG2 /Default /UCR /Bad /UCR2 /Default >>'}),(51,102,153))
        # Sampled, stitched and calculator functions share the same state path.
        bg=stream(bytes((0,64)),'/FunctionType 0 /Domain [0 1] /Range [0 1] /Size [2] /BitsPerSample 8')
        ucr=stream('{ .5 mul }','/FunctionType 4 /Domain [0 1] /Range [-1 1]')
        stitched='<< /FunctionType 3 /Domain [0 1] /Functions [<< /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [.25] /N 1 >>] /Bounds [] /Encode [0 1] >>'
        for bgfunction in (bg,stitched):
            doc=page(DEVICE,'/Half gs .2 .4 .6 rg '+FULL,objects={30:'<< /BG2 70 0 R /UCR2 71 0 R >>',70:bgfunction,71:ucr})
            render(doc,(77,128,179));fault(doc)
        doc=page(DEVICE,'/Half gs .2 .4 .6 rg '+FULL,objects={30:'<< /UCR << /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [-.25] /N 1 >> >>'})
        render(doc,(0,0,26));fault(doc)
        bad=page(DEVICE,'/Half gs 0 g '+FULL,objects={30:'<< /BG2 /Identity >>'})
        render(bad,reject=True);fault(bad,True)
        endpoint='<< /FunctionType 3 /Domain [0 1] /Functions [<< /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [1] /N 1 >> << /FunctionType 2 /Domain [0 1] /N 1 >>] /Bounds [0] /Encode [0 1 0 1] >>'
        doc=page(DEVICE,'/Half gs 1 1 1 rg '+FULL,objects={30:'<< /BG2 '+endpoint+' >>'})
        render(doc,(0,0,0));fault(doc)
        # Page-native CMYK components reach the mask's fourth backdrop component.
        child=form('','/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS /DeviceCMYK >>')
        data=page(DEVICE,'/Half gs 0 g '+FULL,group=False,objects={20:child,30:'<< /SMask << /S /Luminosity /G 20 0 R /BC [0 0 0 .5] >> >>'})
        render(data,(128,128,128));fault(data)
        # The implicit backdrop is the color space's initial value. DeviceCMYK
        # starts at black K=1, while ICCBased starts at zero components.
        for explicit in (False,True):
            mask='<< /SMask << /S /Luminosity /G 20 0 R '+('/BC [0 0 0 1]' if explicit else '')+' >> >>'
            doc=page(DEVICE,'/Half gs 0 g '+FULL,group=False,objects={20:child,30:mask})
            render(doc,(255,255,255));fault(doc)
        white_half=form('0 0 0 0 k 0 0 9 12 re f','/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS /DeviceCMYK >>')
        doc=page(DEVICE,'/Half gs 0 g '+FULL,group=False,objects={20:white_half,30:'<< /SMask << /S /Luminosity /G 20 0 R >> >>'})
        actual=render(doc)
        assert actual==bytes(c for y in range(16) for x in range(24) for c in ((0,0,0,255) if x<12 else (255,255,255,255)))
        fault(doc)
        render(page(DEVICE,'/C cs '+FULL),(0,0,0))
        # Gray/RGB/CMYK cache transitions preserve all four components and can
        # grow the cached color planes without changing the shared definition.
        for shading in (False,True):
            source=(.8,0,0,.6);back=(0,0,0,.2)
            if shading:
                pattern='<< /PatternType 2 /Shading << /ShadingType 2 /ColorSpace /DeviceCMYK /Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [.8 0 0 .6] /C1 [.8 0 0 .6] /N 1 >> /Extend [true true] >> >>'
            else:
                pattern=stream('.8 0 0 .6 k 0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>')
            child=form('0 0 0 .2 k '+FULL+' /A gs /Pattern cs /P scn '+FULL,
                '/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS /DeviceCMYK >> /Resources << /Pattern << /P 40 0 R >> /ExtGState << /A << /ca .5 /BM /Multiply >> >> >>')
            root=page(DEVICE,'/Pattern cs /P scn 0 0 1 1 re f /G Do',group=False,objects={20:child,40:pattern},resources='/Pattern << /P 40 0 R >> /XObject << /G 20 0 R >>')
            values=[(a+b)/2 for a,b in zip(back,mixed('Multiply',source,back))]
            render(root,rgb(values));fault(root)
        # Reselecting a shading pattern in another content stream inherits
        # that stream's initial black-generation state, not a prior cache's.
        pattern='<< /PatternType 2 /Shading << /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [.2 .4 .6] /C1 [.2 .4 .6] /N 1 >> /Extend [true true] >> >>'
        child=form('/Pattern cs /P scn '+FULL,
                   '/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS /DeviceCMYK >> /Resources << /Pattern << /P 40 0 R >> >>')
        doc=page(DEVICE,'/Pattern cs /P scn 0 0 1 1 re f /Half gs /G Do',group=False,
                 objects={20:child,30:'<<'+functions+'>>',40:pattern},
                 resources='/Pattern << /P 40 0 R >> /XObject << /G 20 0 R >>')
        render(doc,(77,128,179));fault(doc)
        # A four-channel ICC destination retains K through composition and
        # converts only when the completed group enters the output space.
        data=tags(profile(forward_lut(4),inputs=4,lab=False),{b'B2A0':lut(4)})
        spec=('ICC CMYK',4,'[/ICCBased 100 0 R]',{100:stream(data,'/N 4')},False)
        source,back=colors[0]
        for mode in MODES:
            values=[(a+b)/2 for a,b in zip(back,mixed(mode,source,back))]
            actual=render(page(spec,color(back)+FULL+' /Half gs '+color(source)+FULL,objects={30:f'<< /ca .5 /BM /{mode} >>'}))
            expected=render(page(spec,color(values)+FULL,group=False))
            assert max(abs(a-b) for a,b in zip(actual,expected))<=1,(mode,actual[:4],expected[:4])
        space='[/CalRGB << /WhitePoint [.964202880859375 1 .8249053955078125] >>]'
        values=[v*32768/65535 for v in (.2,.4,.6)]
        native=(values[0],min(values[1],values[2]),min(values),1-values[2])
        doc=page(spec,'/Source cs .2 .4 .6 scn '+FULL,spaces='/Source '+space)
        assert render(doc)==render(page(spec,color(native)+FULL,group=False))
        fault(doc)
        # CS/cs must not give an ICC CMYK profile the DeviceCMYK K=1 default.
        chosen=tags(profile(forward_lut(4,transform=lambda x:(x[0],x[3],x[1])),inputs=4,lab=False),{b'B2A0':lut(4)})
        spec=('ICC defaults',4,'[/ICCBased 100 0 R]',{100:stream(chosen,'/N 4')},False)
        for grouped in (False,True):
            assert render(page(spec,'/C cs '+FULL,group=grouped))==render(page(spec,color((0,)*4)+FULL,group=grouped))
        child=form('','/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS '+spec[2]+' >>')
        doc=page(spec,'/Half gs 0 g '+FULL,group=False,objects={20:child,30:'<< /SMask << /S /Luminosity /G 20 0 R >> >>'})
        render(doc,(255,255,255));fault(doc)
    print(f'PDF CMYK groups: {component_checks} native component checks; {checks} page checks; {allocations} allocation checks')

if __name__=='__main__':main()
