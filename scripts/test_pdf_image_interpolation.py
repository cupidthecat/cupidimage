#!/usr/bin/env python3
"""Check decoded-component bilinear image and mask sampling against exact fixtures."""
import argparse
import itertools
import math
from pathlib import Path
import struct
import subprocess
import tempfile
from test_pdf_images import fixture, image_stream, RGB
from test_pdf_pages import document
from test_pdf_type3 import stream
from test_pdf_forms import form

W,H=24,16
COLORS=((1,0,0),(0,1,0),(0,0,1),(1,1,0))

def weights(x,y,interpolate=True):
    if not interpolate:
        return tuple(int(i==(y>=H/2)*2+(x>=W/2)) for i in range(4))
    u=max(0,min(1,2*(x+.5)/W-.5));v=max(0,min(1,2*(y+.5)/H-.5))
    return (1-u)*(1-v),u*(1-v),(1-u)*v,u*v

def raster(colors=COLORS,visible=(1,1,1,1),mask=None,image_filter=True,mask_filter=False):
    result=bytearray()
    for y in range(H):
        for x in range(W):
            taps=weights(x,y,image_filter)
            shape=sum(w*a for w,a in zip(taps,visible))
            alpha=shape*(sum(w*a for w,a in zip(weights(x,y,mask_filter),mask)) if mask else 1)
            rgb=[sum(w*a*c[k] for w,a,c in zip(taps,visible,colors))/shape if shape else 0 for k in range(3)]
            result.extend(max(0,min(255,math.floor(math.nextafter(c*255*alpha+127.5*(1-alpha)+.5,math.inf)))) for c in rgb)
            result.append(255)
    return bytes(result)

def packed(values,bits):
    output=bytearray()
    for row in (values[:2],values[2:]):
        value=0
        for sample in row:value=(value<<bits)|sample
        size=(bits*2+7)//8
        output.extend((value<<(size*8-bits*2)).to_bytes(size,'big'))
    return bytes(output)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe',type=Path);parser.add_argument('allocator',type=Path)
    args=parser.parse_args();checks=allocations=0
    attrs='/Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB'
    with tempfile.TemporaryDirectory(prefix='cupid-image-interpolation-') as temp:
        path=Path(temp)/'case.pdf'
        def render(data,reject=False):
            nonlocal checks
            path.write_bytes(data)
            r=subprocess.run([str(args.probe.resolve()),str(path),'0'],capture_output=True,timeout=30)
            assert r.returncode==(3 if reject else 0),(r.returncode,r.stderr)
            checks+=1
            if reject:return None
            assert struct.unpack('<III',r.stdout[:12])==(W,H,1)
            return r.stdout[12:]
        def fault(data,reject=False):
            nonlocal allocations
            path.write_bytes(data)
            r=subprocess.run([str(args.allocator.resolve()),str(path),str(int(reject))],capture_output=True,text=True,timeout=120,check=True)
            allocations+=int(r.stdout.split('checks')[0].split()[-1])
        expected=raster()
        data=fixture(attrs=attrs+' /Interpolate true')
        assert render(data)==expected
        fault(data)
        baseline=render(fixture())
        assert baseline!=expected
        for value in ('false','null','20 0 R','21 0 R','999 0 R'):
            assert render(fixture(attrs=attrs+' /Interpolate '+value,extra={20:b'null',21:b'20 0 R'}))==baseline
        assert render(fixture(attrs=attrs+' /Interpolate 20 0 R',extra={20:b'true'}))==expected
        for value in ('1','/true','[]','(true)'):
            data=fixture(attrs=attrs+' /Interpolate '+value)
            render(data,True);fault(data,True)
        for bits in (1,2,4,8,16):
            payload=bytearray();maximum=(1<<bits)-1
            for row in (COLORS[:2],COLORS[2:]):
                packed_row=0
                for color in row:
                    for component in color:packed_row=(packed_row<<bits)|int(component*maximum)
                size=(6*bits+7)//8
                payload.extend((packed_row<<(size*8-6*bits)).to_bytes(size,'big'))
            data=fixture(bytes(payload),f'/Width 2 /Height 2 /BitsPerComponent {bits} /ColorSpace /DeviceRGB /Interpolate true')
            assert render(data)==expected,('RGB bits',bits)
        for matrix in ((0,12,-18,0,18,0),(-18,0,0,12,18,0),
                       (12,0,6,12,0,0),(9,0,0,6,4.5,3),(.75,0,0,.75,6,6)):
            a,b,c,d,e,f=matrix;det=a*d-b*c;target=bytearray()
            for y in range(H):
                for x in range(W):
                    tx=(x+.5)*.75-e;ty=12-(y+.5)*.75-f
                    u=(d*tx-c*ty)/det;v=(a*ty-b*tx)/det
                    if not (0<=u<1 and 0<v<=1):
                        target.extend((128,128,128,255));continue
                    sx=max(0,min(1,2*u-.5));sy=max(0,min(1,2*(1-v)-.5))
                    taps=((1-sx)*(1-sy),sx*(1-sy),(1-sx)*sy,sx*sy)
                    target.extend(math.floor(math.nextafter(sum(w*c[k] for w,c in zip(taps,COLORS))*255+.5,math.inf)) for k in range(3))
                    target.append(255)
            data=fixture(attrs=attrs+' /Interpolate true',content=' '.join(map(str,matrix))+' cm /Im0 Do')
            assert render(data)==bytes(target),('affine',matrix)
        for width,height,colors in ((1,1,((1,0,0),)),(1,2,((1,0,0),(0,0,1))),(2,1,((1,0,0),(0,1,0)))):
            payload=bytes(int(v*255) for color in colors for v in color)
            data=fixture(payload,f'/Width {width} /Height {height} /BitsPerComponent 8 /ColorSpace /DeviceRGB /Interpolate true')
            expanded=(colors[0],colors[-1],colors[0],colors[-1]) if height==1 else (colors[0],colors[0],colors[-1],colors[-1])
            assert render(data)==raster(expanded),('single axis',width,height)
        # Palette indices must be expanded before interpolation.
        palette='[ /Indexed /DeviceRGB 3 <'+RGB.hex()+'> ]'
        for bits in (2,4,8,16):
            data=fixture(packed([0,1,2,3],bits),f'/Width 2 /Height 2 /BitsPerComponent {bits} /ColorSpace {palette} /Interpolate true')
            assert render(data)==expected,('indexed',bits)
        for bits in (1,2,4,8,16):
            maximum=(1<<bits)-1;samples=[0,maximum,maximum,0]
            for decode in ('[0 1]','[1 0]','[-1 2]'):
                colors=[(v/maximum,)*3 for v in samples]
                if decode=='[1 0]':colors=[(1-c[0],)*3 for c in colors]
                data=fixture(packed(samples,bits),f'/Width 2 /Height 2 /BitsPerComponent {bits} /ColorSpace /DeviceGray /Interpolate true /Decode {decode}')
                assert render(data)==raster(colors),('gray',bits,decode)
        # Color-key transparency filters coverage, not the hidden red color.
        data=fixture(attrs=attrs+' /Interpolate true /Mask [255 255 0 0 0 0]')
        assert render(data)==raster(visible=(0,1,1,1))
        fault(data)
        for decode,visible in (('[0 1]',(1,0,0,1)),('[1 0]',(0,1,1,0))):
            data=fixture(b'\x40\x80',f'/Width 2 /Height 2 /ImageMask true /Interpolate true /Decode {decode}',content='1 0 0 rg 18 0 0 12 0 0 cm /Im0 Do')
            assert render(data)==raster(((1,0,0),)*4,visible)
            fault(data)
        # Parent and mask interpolation flags are independent.
        for soft,image_filter,mask_filter in itertools.product((False,True),(False,True),(False,True)):
            mask_values=(0,1/3,2/3,1) if soft else (1,0,0,1)
            mask_attrs='/Width 2 /Height 2 '+('/ColorSpace /DeviceGray /BitsPerComponent 8' if soft else '/ImageMask true')
            mask_data=bytes((0,85,170,255)) if soft else b'\x40\x80'
            mask_object=image_stream(mask_data,mask_attrs+f' /Interpolate {str(mask_filter).lower()}')
            data=fixture(bytes((255,0,0))*4,attrs+f' /Interpolate {str(image_filter).lower()} /'+('SMask' if soft else 'Mask')+' 7 0 R',extra={7:mask_object})
            assert render(data)==raster(((1,0,0),)*4,mask=mask_values,image_filter=image_filter,mask_filter=mask_filter),(soft,image_filter,mask_filter)
            if image_filter and mask_filter:fault(data)
        # Mask resolution is independent of the image's 2-by-2 sample grid.
        for width,height,mask_samples in ((1,2,(0,255)),(4,1,(0,0,255,255))):
            mask_object=image_stream(bytes(mask_samples),f'/Width {width} /Height {height} /BitsPerComponent 8 /ColorSpace /DeviceGray /Interpolate true')
            data=fixture(bytes((255,0,0))*4,attrs+' /SMask 7 0 R',extra={7:mask_object})
            target=bytearray()
            for y in range(H):
                for x in range(W):
                    alpha=max(0,min(1,2*(y+.5)/H-.5)) if width==1 else max(0,min(1,4*(x+.5)/W-1.5))
                    target.extend(math.floor(math.nextafter(c*255*alpha+127.5*(1-alpha)+.5,math.inf)) for c in (1,0,0))
                    target.append(255)
            assert render(data)==bytes(target),('independent mask grid',width,height)
            fault(data)
        # Recover preblended colors before filtering, carrying alpha through
        # the filter so undefined zero-alpha texels cannot leave a matte halo.
        for mask_filter in (False,True):
            mask_values=(0,1/3,2/3,1)
            payload=bytes(c for a in (0,85,170,255) for c in (255,255-a,255-a))
            mask_object=image_stream(bytes((0,85,170,255)),
                '/Width 2 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 8 /Matte [1 1 1] '+
                f'/Interpolate {str(mask_filter).lower()}')
            data=fixture(payload,attrs+' /Interpolate true /SMask 7 0 R',extra={7:mask_object})
            assert render(data)==raster(((1,0,0),)*4,mask=mask_values,mask_filter=mask_filter),('Matte',mask_filter)
            fault(data)
        # A knockout group distinguishes shape from opacity. Explicit masks
        # filter shape; soft masks affect shape only when alpha-is-shape is set.
        for soft,ais,opacity in itertools.product((False,True),(False,True),(.5,1)):
            mask_values=(0,1/3,2/3,1) if soft else (1,0,0,1)
            mask_attrs='/Width 2 /Height 2 /Interpolate true '+('/ColorSpace /DeviceGray /BitsPerComponent 8' if soft else '/ImageMask true')
            mask_object=image_stream(bytes((0,85,170,255)) if soft else b'\x40\x80',mask_attrs)
            image=image_stream(bytes((0,0,255))*4,attrs+' /'+('SMask' if soft else 'Mask')+' 7 0 R')
            group=form('1 0 0 rg 0 0 18 12 re f /E gs 18 0 0 12 0 0 cm /Im0 Do',
                '/BBox [0 0 18 12] /Group << /S /Transparency /I true /K true /CS /DeviceRGB >> '+
                '/Resources << /XObject << /Im0 5 0 R >> /ExtGState << /E 9 0 R >> >>')
            data=document('/MediaBox [0 0 18 12] /Resources << /XObject << /G 8 0 R >> >>',
                stream='/G Do',extra={5:image,7:mask_object,8:group,9:f'<< /ca {opacity} /AIS {str(ais).lower()} >>'})
            target=bytearray()
            for y in range(H):
                for x in range(W):
                    mask_alpha=sum(w*a for w,a in zip(weights(x,y),mask_values))
                    alpha=mask_alpha*opacity
                    shape=alpha if ais else 1 if soft else mask_alpha
                    target.extend(math.floor(math.nextafter(c*255+.5,math.inf)) for c in (1-alpha,shape-alpha,shape))
                    target.append(255)
            assert render(data)==bytes(target),('knockout',soft,ais,opacity)
            if opacity==.5:fault(data)
        # Type 3 glyph image execution uses the same sampler and restores state.
        font='<< /Type /Font /Subtype /Type3 /FontBBox [0 0 18 12] /FontMatrix [1 0 0 1 0 0] /FirstChar 65 /LastChar 65 /Widths [18] /Encoding << /Differences [65 /A] >> /CharProcs << /A 7 0 R >> /Resources << /XObject << /Im0 5 0 R >> >> >>'
        glyph=stream('18 0 d0 18 0 0 12 0 0 cm /Im0 Do')
        data=document('/MediaBox [0 0 18 12] /Resources << /Font << /F 6 0 R >> >>',
            stream='.5 g 0 0 18 12 re f BT /F 1 Tf (A) Tj ET',
            extra={5:image_stream(RGB,attrs+' /Interpolate true'),6:font,7:glyph})
        assert render(data)==expected,'Type 3 image sampling'
        fault(data)
        inline='18 0 0 12 0 0 cm BI /W 2 /H 2 /BPC 8 /CS /RGB /I true /F /AHx ID '+RGB.hex()+'> EI'
        assert render(fixture(content=inline))==expected,'inline I abbreviation'
    print(f'PDF image interpolation: {checks} page checks; {allocations} allocation checks')

if __name__=='__main__':
    main()
