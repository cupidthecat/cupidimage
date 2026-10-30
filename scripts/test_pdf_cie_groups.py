#!/usr/bin/env python3
"""Qualify calibrated/ICC blending, color transport, caches and mask luminosity."""
import argparse
import itertools
import math
from pathlib import Path
import struct
import subprocess
import tempfile
from test_pdf_pages import document
from test_pdf_forms import form
from test_pdf_type3 import stream
from test_pdf_icc import profile, gamma, curve, parametric, xyz
from test_pdf_intents import tags
from test_pdf_icc_lut import lut
from test_pdf_calibrated import D65,M65,encode
from test_pdf_images import image_stream

W,H=24,16
FULL='0 0 18 12 re f'

def specifications():
    result=[('CalGray',1,'[/CalGray << /WhitePoint ['+D65+'] /Gamma 2 >>]',{},False),
            ('CalRGB',3,'[/CalRGB << /WhitePoint ['+D65+'] /Gamma [2 2 2] /Matrix ['+M65+'] >>]',{},False)]
    for name,n,lab,version in (('ICCGray',1,False,4),('ICCRGB',3,False,4),
                              ('ICCGrayLab',1,True,4),('ICCRGB-v2',3,False,2)):
        data=profile([gamma(2)]*n,gray=n==1,lab=lab,version=version)
        result.append((name,n,'[/ICCBased 100 0 R]',{100:stream(data,f'/N {n}'),101:stream(data,f'/N {n}')},lab))
    return result

def color(values):
    return '/C cs '+' '.join(map(str,values))+' scn '

def page(spec,content,group=True,objects=None,resources='',group_flags='',spaces='',fonts=''):
    name,n,space,base,lab=spec
    extra={**base,30:'<< /ca .5 >>',31:'<< /ca .5 >>',50:'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'}
    extra.update(objects or {})
    attrs='/MediaBox [0 0 18 12] /Resources << /ColorSpace << /C '+space+' '+spaces+' >> /Font << /F 50 0 R '+fonts+' >> /ExtGState << /Half 30 0 R /Outer 31 0 R >> '+resources+' >>'
    if group:attrs+=' /Group << /S /Transparency /CS '+space+' '+group_flags+' >>'
    return document(attrs,stream=content,extra=extra)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('probe',type=Path);parser.add_argument('allocator',type=Path);args=parser.parse_args()
    checks=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupid-cie-groups-') as temp:
        path=Path(temp)/'case.pdf'
        def render(data,reject=False,aa=False):
            nonlocal checks
            path.write_bytes(data)
            result=subprocess.run([str(args.probe.resolve()),str(path),'0',*(['--pdf-text-aa'] if aa else [])],capture_output=True,timeout=30)
            assert result.returncode==(3 if reject else 0),(result.returncode,result.stderr)
            checks+=1
            if reject:return None
            assert struct.unpack('<III',result.stdout[:12])==(W,H,1)
            return result.stdout[12:]
        def fault(data,reject=False):
            nonlocal allocations
            path.write_bytes(data)
            result=subprocess.run([str(args.allocator.resolve()),str(path),str(int(reject))],capture_output=True,text=True,check=True,timeout=120)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        for spec in specifications():
            name,n,space,base,lab=spec
            cb=(.25,) if n==1 else (.25,.5,.75)
            cs=(.75,) if n==1 else (.75,.25,.5)
            for mode in ('Normal','Multiply','Screen','Difference'):
                blend=lambda a,b: b if mode=='Normal' else a*b if mode=='Multiply' else a+b-a*b if mode=='Screen' else abs(a-b)
                values=tuple((a+blend(a,b))/2 for a,b in zip(cb,cs))
                data=page(spec,color(cb)+FULL+' /Half gs '+color(cs)+FULL,
                          objects={30:f'<< /ca .5 /BM /{mode} >>'})
                assert render(data)==render(page(spec,color(values)+FULL,group=False)),(name,mode,'blend components')
                if mode=='Normal':fault(data)
            # Device sources are converted into the calibrated encoding before
            # blending. This invokes the destination inverse, not just its parser.
            device_values=(.75,) if n==1 else (.75,.5,.25)
            operator='g' if n==1 else 'rg'
            body=color((0,)*n)+FULL+' /Half gs '+' '.join(map(str,device_values))+' '+operator+' '+FULL
            actual=render(page(spec,body))
            expected_values=[]
            for v in device_values:
                linear=v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4
                if lab:
                    l=116*linear**(1/3)-16 if linear>216/24389 else linear*24389/27
                    l*=.25
                    linear=((l+16)/116)**3 if l>8 else l*27/24389
                else:linear*=.25
                expected_values.append(encode(linear))
            if n==1:expected_values*=3
            assert all(abs(actual[i+c]-expected_values[c])<=1 for i in range(0,len(actual),4) for c in range(3)),(name,'device-to-blend inverse',actual[:4],expected_values)
            fault(page(spec,body))
            # Image samples enter the same space before their soft-mask opacity.
            image=image_stream(bytes([128]*n),f'/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace {space} /SMask 22 0 R')
            mask=image_stream(bytes([128]),'/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray')
            data=page(spec,color((0,)*n)+FULL+' q 18 0 0 12 0 0 cm /Im Do Q',objects={21:image,22:mask},resources='/XObject << /Im 21 0 R >>')
            assert render(data)==render(page(spec,color(((128/255)**2,)*n)+FULL,group=False)),(name,'image source and mask')
            fault(data)
            # All page isolation flags retain isolated-page semantics.
            expected=render(page(spec,color((.5,)*n)+FULL,group=False))
            for flags in ('','/I false','/I true','/I true /K true'):
                assert render(page(spec,color((.5,)*n)+FULL,group_flags=flags))==expected,(name,'page isolation',flags)
            # A nonisolated subgroup uses the same native-color backdrop and
            # returns its source once; caller opacity is applied after the group.
            local='/Resources << /ColorSpace << /C '+space+' >> /ExtGState << /Half 30 0 R >> >>'
            subgroup=form('/Half gs '+color(cs)+FULL,
                          '/BBox [0 0 18 12] /Group << /S /Transparency /I false /CS '+space+' >> '+local)
            data=page(spec,color(cb)+FULL+' /Outer gs /G Do',objects={20:subgroup},resources='/XObject << /G 20 0 R >>')
            nonisolated_doc=data
            target=tuple(a+(b-a)*.25 for a,b in zip(cb,cs))
            assert render(data)==render(page(spec,color(target)+FULL,group=False)),(name,'nonisolated source')
            fault(data)
            # An isolated child converts into its RGB parent before caller alpha.
            child=form(color((.5,)*n)+FULL,'/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS '+space+' >> '+local)
            actual=page(spec,'/Outer gs /G Do',group=False,objects={20:child},resources='/XObject << /G 20 0 R >>')
            assert render(actual)==render(page(spec,'/Outer gs '+color((.5,)*n)+FULL,group=False)),(name,'isolated transfer')
            fault(actual)
            # Colored and uncolored Type 3 glyph surfaces inherit the blending
            # encoding; caller color is applied to a d1 shape only once.
            for uncolored in (False,True):
                program=('1000 0 0 0 1000 1000 d1 0 0 1000 1000 re f' if uncolored else
                         '1000 0 d0 '+color((0,)*n)+'0 0 1000 1000 re f /Half gs '+color((1,)*n)+'0 0 1000 1000 re f')
                font='<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [.001 0 0 .001 0 0] /FirstChar 65 /LastChar 65 /Widths [1000] /Encoding << /Differences [65 /A] >> /CharProcs << /A 61 0 R >> /Resources << /ColorSpace << /C '+space+' >> /ExtGState << /Half 30 0 R >> >> >>'
                data=page(spec,color((.5,)*n)+'BT /F3 18 Tf (A) Tj ET',objects={60:font,61:stream(program)},fonts='/F3 60 0 R')
                assert render(data)==render(page(spec,color((.5,)*n)+FULL,group=False)),(name,'Type 3 native blend space',uncolored)
                fault(data)
            # Separate ICC objects with identical profile bytes are the same
            # blend encoding, not a prohibited nonisolated space change.
            if base:
                alias='[/ICCBased 101 0 R]'
                alias_child=subgroup.replace(space.encode(),alias.encode())
                alias_page=page(spec,color(cb)+FULL+' /Outer gs /G Do',objects={20:alias_child},resources='/XObject << /G 20 0 R >>')
                assert render(alias_page)==render(nonisolated_doc),(name,'profile equivalence')
                fault(alias_page)
            # Luminosity is the CIE Y component after compositing in the group,
            # rather than a weighted sum of encoded components.
            for alpha,background in itertools.product((1,.5),(0,1)):
                mask_group=form(('/Inner gs ' if alpha==.5 else '')+color((.5,)*n)+FULL,
                    '/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS '+space+' >> '+local.replace('/Half 30 0 R','/Inner 32 0 R'))
                raw=background*(1-alpha)+.5*alpha
                luminance=raw*raw
                if lab:
                    l=luminance*100
                    luminance=((l+16)/116)**3 if l>8 else l*27/24389
                mask_state='<< /SMask << /S /Luminosity /G 20 0 R /BC ['+' '.join([str(background)]*n)+'] >> >>'
                mask_doc=page(spec,'/Half gs 0 g '+FULL,group=False,objects={20:mask_group,30:mask_state,32:'<< /ca .5 >>'})
                pixels=render(mask_doc);expected_byte=round(255*(1-luminance))
                assert all(abs(pixels[i+c]-expected_byte)<=1 for i in range(0,len(pixels),4) for c in range(3)),(name,alpha,background,'CIE mask luminosity',pixels[:4],expected_byte)
                if alpha==.5 and not background:fault(mask_doc)
            # Text coverage is blended in the selected encoding as well.
            background=color((1,)*n)+FULL
            glyph='BT /F 12 Tf 2 1 Td (A) Tj ET'
            actual=render(page(spec,background+color((0,)*n)+glyph),aa=True)
            device=('DeviceGray',1,'/DeviceGray',{},False)
            coverage=render(page(device,'1 g '+FULL+' 0 g '+glyph),aa=True)
            for i in range(0,len(actual),4):
                y=(coverage[i]/255)**2
                if lab:
                    l=y*100;y=((l+16)/116)**3 if l>8 else l*27/24389
                expected_byte=encode(y)
                assert all(abs(actual[i+c]-expected_byte)<=1 for c in range(3)),(name,'text coverage',actual[i:i+4],expected_byte)
        # Gamut clipping must wait until conversion into the blend/output space.
        # Rec.2020 green lies outside the fixed output gamut. An intermediate RGB
        # clamp would change this result even though both CIE spaces are valid.
        wide_matrix='.636958 .2627002 0 .1446169 .6779981 .0280727 .1688809 .0593017 1.0609851'
        wide='[/CalRGB << /WhitePoint ['+D65+'] /Matrix ['+wide_matrix+'] >>]'
        nonlinear='[/CalRGB << /WhitePoint ['+D65+'] /Matrix ['+wide_matrix+'] /Gamma [2 2 2] >>]'
        spec=('wide',3,wide,{},False)
        data=page(spec,color((0,0,0))+FULL+' /Half gs /Source cs 0 1 0 scn '+FULL,
                  spaces='/Source '+nonlinear)
        assert render(data)==render(page(spec,color((0,.5,0))+FULL,group=False)),'premature gamut clipping'
        fault(data)
        # Colored tiling and shading caches must be keyed by the actual blend
        # encoding, not merely by gray versus RGB component count.
        specs=specifications();source=specs[2];destination=specs[3]
        source_profile=profile([gamma(2)],gray=True)
        target_profile=profile([gamma(1)],gray=True)
        source_space='[/ICCBased 100 0 R]';target_space='[/ICCBased 102 0 R]'
        source=('cache',1,source_space,{100:stream(source_profile,'/N 1'),102:stream(target_profile,'/N 1')},False)
        for shading,reverse in itertools.product((False,True),(False,True)):
            if shading:
                pattern='<< /Type /Pattern /PatternType 2 /Shading 41 0 R >>'
                extra={41:'<< /ShadingType 2 /ColorSpace '+source_space+' /Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [.5] /C1 [.5] /N 1 >> /Extend [true true] >>'}
            else:
                pattern=stream('/Source cs .5 scn 0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << /ColorSpace << /Source '+source_space+' >> >>')
                extra={}
            child=form('/Pattern cs /P scn 9 0 9 12 re f',
                       '/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS '+target_space+' >> /Resources << /Pattern << /P 40 0 R >> >>')
            first='/Pattern cs /P scn 0 0 9 12 re f '
            content='/G Do '+first if reverse else first+'/G Do'
            data=page(source,content,objects={20:child,40:pattern,**extra},resources='/Pattern << /P 40 0 R >> /XObject << /G 20 0 R >>')
            expected=render(page(source,color((.5,))+FULL,group=False))
            assert render(data)==expected,('pattern blend-space cache',shading,reverse)
            fault(data)
        # Equal component counts do not make different nonisolated encodings valid.
        child=form('0 g '+FULL,'/BBox [0 0 18 12] /Group << /S /Transparency /I false /CS '+target_space+' >>')
        data=page(source,'/G Do',objects={20:child},resources='/XObject << /G 20 0 R >>')
        render(data,True);fault(data,True)
        # PDF numeric zero has one value, even when parsed from a signed spelling.
        parent_space='[/CalRGB << /WhitePoint ['+D65+'] /Matrix [1 0 0 0 1 0 0 0 1] >>]'
        child_space='[/CalRGB << /WhitePoint ['+D65+'] /Matrix [1 -0 0 0 1 -0 -0 0 1] >>]'
        spec=('signed zero',3,parent_space,{},False)
        child=form('0 0 0 rg '+FULL,'/BBox [0 0 18 12] /Group << /S /Transparency /I false /CS '+child_space+' >>')
        actual=page(spec,'/G Do',objects={20:child},resources='/XObject << /G 20 0 R >>')
        assert render(actual)==render(page(spec,'0 0 0 rg '+FULL))
        fault(actual)
        # An inverse that underflows to the wrong color must fail, not silently
        # return black for a middle-gray source.
        tiny=('tiny gamma',1,'[/CalGray << /WhitePoint ['+D65+'] /Gamma .0001 >>]',{},False)
        bad=page(tiny,'.5 g '+FULL)
        render(bad,True);fault(bad,True)
        data=profile([parametric(0,[1/65536])],gray=True)
        tiny=('tiny ICC gamma',1,'[/ICCBased 100 0 R]',{100:stream(data,'/N 1')},False)
        bad=page(tiny,'.5 g '+FULL)
        render(bad,True);fault(bad,True)
        # Intent belongs to the painting object. A page's final conversion uses
        # its initial intent, while a group's Do conversion uses the caller's.
        absolute=bytearray(tags(profile([gamma(2)],gray=True),
            {b'wtpt':xyz((0xf6d6/65536,.5,0xd32d/65536))}))
        absolute[12:16]=b'scnr'
        spec=('absolute gray',1,'[/ICCBased 100 0 R]',{100:stream(bytes(absolute),'/N 1')},False)
        relative=render(page(spec,color((.5,))+FULL,group=False))
        assert render(page(spec,color((.5,))+FULL+' /AbsoluteColorimetric ri'))==relative,'page default intent'
        local='/Resources << /ColorSpace << /C '+spec[2]+' >> >>'
        child=form(color((.5,))+FULL,'/BBox [0 0 18 12] /Group << /S /Transparency /I true /CS '+spec[2]+' >> '+local)
        data=page(spec,'/AbsoluteColorimetric ri /G Do',group=False,objects={20:child},resources='/XObject << /G 20 0 R >>')
        assert render(data)==render(page(spec,'/AbsoluteColorimetric ri '+color((.5,))+FULL,group=False)),'group invocation intent'
        mask_state='<< /SMask << /S /Luminosity /G 20 0 R >> >>'
        mask_doc=page(spec,'/AbsoluteColorimetric ri /Half gs 0 g '+FULL,group=False,objects={20:child,30:mask_state})
        assert render(mask_doc)==bytes((223,223,223,255))*(W*H),'absolute CIE luminosity scaling'
        fault(mask_doc)
        # A selected table pipeline takes precedence over matrix/TRC tags. Its
        # missing destination transform must not be replaced by that matrix.
        data=profile(extra=[(b'A2B0',lut())])
        spec=('table precedence',3,'[/ICCBased 100 0 R]',{100:stream(data,'/N 3')},False)
        render(page(spec,color((.5,)*3)+FULL,group=False))
        bad=page(spec,color((.5,)*3)+FULL)
        render(bad,True);fault(bad,True)
        # Noninvertible profiles remain usable as sources but not blend spaces.
        for data in (profile([curve([1234,1234])]*3),profile([curve([0,65535,32768,65535])]*3)):
            spec=('noninvertible',3,'[/ICCBased 100 0 R]',{100:stream(data,'/N 3')},False)
            render(page(spec,color((.5,)*3)+FULL,group=False))
            bad=page(spec,color((.5,)*3)+FULL)
            render(bad,True);fault(bad,True)
    print(f'PDF CIE blending: {checks} page checks; {allocations} allocation checks')

if __name__=='__main__':main()
