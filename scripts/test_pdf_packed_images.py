#!/usr/bin/env python3
"""Known-pixel tests for packed/16-bit PDF images, predictors, and masks."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_images import fixture
from audit_pdf_corpus import read_ppm


def pack(values, bits):
    result = bytearray((len(values)*bits+7)//8)
    for i,value in enumerate(values):
        if bits == 16: struct.pack_into('>H',result,i*2,value)
        elif bits == 8: result[i]=value
        else: result[i*bits//8] |= value << (8-bits-(i*bits%8))
    return bytes(result)


def encode(rows, components, bits, predictor):
    maximum=(1<<bits)-1
    if predictor==2:
        return b''.join(pack([(v-row[i-components])&maximum if i>=components else v for i,v in enumerate(row)],bits) for row in rows)
    packed=[pack(row,bits) for row in rows]
    if predictor<10:return b''.join(packed)
    bpp=(components*bits+7)//8;output=bytearray()
    for y,row in enumerate(packed):
        kind=predictor-10 if predictor<15 else 3+y
        output.append(kind)
        for x,value in enumerate(row):
            left=row[x-bpp]if x>=bpp else 0;up=packed[y-1][x]if y else 0
            ul=packed[y-1][x-bpp]if y and x>=bpp else 0
            p=left+up-ul;paeth=min((left,up,ul),key=lambda v:abs(p-v))
            output.append((value-(0,left,up,(left+up)//2,paeth)[kind])&255)
    return bytes(output)


def main():
    probe=str(Path(sys.argv[1]).resolve());reference='--reference' in sys.argv
    count=references=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-pdf-packed-')as temporary:
        directory=Path(temporary)
        def check(name,data,expected=None,reject=False,compare_reference=True,secondary_reference=False):
            nonlocal count,references
            count+=1;path=directory/(name+'.pdf');path.write_bytes(data.replace(b'%PDF-1.4',b'%PDF-1.7'))
            result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert result.returncode==(3 if reject else 0),(name,result.stderr)
            if reject:return
            assert struct.unpack('=III',result.stdout[:12])==(24,16,1),name
            pixels=result.stdout[12:];assert len(pixels)==24*16*4
            wanted=bytes(c for y in range(16) for x in range(24) for c in (*expected(x,y),255))
            assert pixels==wanted,(name,next((i,a,b)for i,(a,b)in enumerate(zip(pixels,wanted))if a!=b))
            if reference and compare_reference:
                use_secondary = secondary_reference or '-16' in name
                if use_secondary:
                    # Use a second oracle for full 16-bit sample/predictor/mask
                    # precision; core expectations remain analytic.
                    import fitz
                    with fitz.open(path) as reference_document:
                        raster=reference_document[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                        w,h,rgb=raster.width,raster.height,raster.samples
                else:
                    prefix=directory/'ref'
                    subprocess.run(['pdftoppm','-singlefile','-r','96',str(path),str(prefix)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,check=True,timeout=15)
                    w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                assert(w,h)==(24,16)
                actual=bytes(c for i,c in enumerate(pixels)if i%4!=3)
                tolerance = 1 if use_secondary else 0  # Tie rounding for the gray background.
                difference = max(abs(a-b) for a,b in zip(actual,rgb))
                assert difference <= tolerance,(name,'independent raster differs',difference)
                references+=1
        for bits in (1,2,4,8,16):
            maximum=(1<<bits)-1
            gray=[0,maximum,maximum//3,2*maximum//3,maximum//2,maximum]
            colors=[(1,0,0),(0,1,0),(0,0,1),(0,1,1),(1,0,1),(1,1,0)]
            for components,space in ((1,'DeviceGray'),(3,'DeviceRGB'),(4,'DeviceCMYK')):
                if components==1:values=gray
                elif components==3:values=[c*maximum for rgb in colors for c in rgb]
                else:values=[c*maximum for rgb in colors for c in (1-rgb[0],1-rgb[1],1-rgb[2],0)]
                rows=[values[:3*components],values[3*components:]]
                def expected(x,y):
                    i=(y//8)*3+x//8
                    if components==1:return (int(gray[i]*255/maximum+.5),)*3
                    return tuple(c*255 for c in colors[i])
                attrs=f'/Width 3 /Height 2 /BitsPerComponent {bits} /ColorSpace /{space}'
                for predictor in (0,1,2,10,11,12,13,14,15):
                    payload=encode(rows,components,bits,predictor)
                    params=''
                    if predictor:
                        payload=zlib.compress(payload)
                        params=f' /Filter /FlateDecode /DecodeParms << /Predictor {predictor} /Colors {components} /Columns 3 /BitsPerComponent {bits} >>'
                    check(f'{space}-{bits}-{predictor}',fixture(payload,attrs+params),expected,compare_reference=components!=4,secondary_reference=(bits==16 and predictor==2))
            attrs=f'/Width 3 /Height 2 /BitsPerComponent {bits} /ColorSpace /DeviceGray'
            payload=b''.join(pack(row,bits)for row in (gray[:3],gray[3:]))
            check(f'gray-key-{bits}',fixture(payload,attrs+f' /Mask [{maximum} {maximum}]'),
                  lambda x,y:(128,128,128)if gray[(y//8)*3+x//8]==maximum else (int(gray[(y//8)*3+x//8]*255/maximum+.5),)*3)
            check(f'inverted-key-{bits}',fixture(payload,attrs+f' /Mask [0 0] /Decode [1 0]'),
                  lambda x,y:(128,128,128)if gray[(y//8)*3+x//8]==0 else (int((1-gray[(y//8)*3+x//8]/maximum)*255+.5),)*3)
        mask=b'\x40\xa0'
        for inverse in (False,True):
            attrs='/Width 3 /Height 2 /ImageMask true'+(' /Decode [1 0]'if inverse else '')
            for compressed in (False,True):
                payload=zlib.compress(mask)if compressed else mask
                params=' /Filter /FlateDecode'if compressed else ''
                check(f'stencil-{inverse}-{compressed}',fixture(payload,attrs+params,content='0 1 0 rg 18 0 0 12 0 0 cm /Im0 Do'),
                      lambda x,y:(0,255,0)if (((y//8)*3+x//8)%2==int(inverse))else(128,128,128))
        for name,attrs,payload in [('bad-depth','/Width 3 /Height 2 /BitsPerComponent 3 /ColorSpace /DeviceGray',b'\0'*3),
                                  ('bad-mask-depth','/Width 3 /Height 2 /BitsPerComponent 8 /ImageMask true',bytes(6)),
                                  ('bad-mask-space','/Width 3 /Height 2 /ImageMask true /ColorSpace /DeviceGray',mask),
                                  ('bad-mask-decode','/Width 3 /Height 2 /ImageMask true /Decode [0 2]',mask),
                                  ('bad-key-range','/Width 3 /Height 2 /BitsPerComponent 1 /ColorSpace /DeviceGray /Mask [1 0]',mask),
                                  ('bad-key-overflow','/Width 3 /Height 2 /BitsPerComponent 1 /ColorSpace /DeviceGray /Mask [0 2]',mask),
                                  ('short-data','/Width 3 /Height 2 /BitsPerComponent 1 /ColorSpace /DeviceGray',b'\0')]:
            check(name,fixture(payload,attrs),reject=True)
    print(f'Packed PDF images: {count} checks passed; {references} independent reference comparisons')


if __name__=='__main__':main()
