#!/usr/bin/env python3
"""Optional checks against installed profiles and an independent color engine."""
import argparse
import ctypes as c
import ctypes.util
import hashlib
import itertools
import json
from pathlib import Path
import random
import subprocess
import tempfile
from test_pdf_icc_lut import fixture


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe',type=Path)
    parser.add_argument('profiles',nargs='+',type=Path)
    parser.add_argument('--samples',type=int,default=40)
    args=parser.parse_args()
    if args.samples<16 or args.samples>4096:parser.error('--samples must be between 16 and 4096')
    path=ctypes.util.find_library('lcms2')
    if not path:parser.error('The optional independent color engine is unavailable')
    lib=c.CDLL(path)
    lib.cmsOpenProfileFromMem.argtypes=[c.c_void_p,c.c_uint32];lib.cmsOpenProfileFromMem.restype=c.c_void_p
    lib.cmsCreate_sRGBProfile.restype=c.c_void_p
    lib.cmsCreateTransform.argtypes=[c.c_void_p,c.c_uint32,c.c_void_p,c.c_uint32,c.c_uint32,c.c_uint32]
    lib.cmsCreateTransform.restype=c.c_void_p
    lib.cmsDoTransform.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_uint32]
    lib.cmsDeleteTransform.argtypes=[c.c_void_p];lib.cmsCloseProfile.argtypes=[c.c_void_p]
    rows=[]
    with tempfile.TemporaryDirectory(prefix='cupidimage-profile-comparison-') as tmp:
        pdf=Path(tmp)/'case.pdf'
        for profile in args.profiles:
            with profile.open('rb') as incoming:
                raw=incoming.read(16*1024*1024+1)
            assert 132<=len(raw)<=16*1024*1024, 'Profile size exceeds the supported bound'
            inputs,space={b'GRAY':(1,3),b'RGB ':(3,4),b'CMYK':(4,6)}[raw[16:20]]
            buffer=c.create_string_buffer(raw)
            source=lib.cmsOpenProfileFromMem(buffer,len(raw));target=lib.cmsCreate_sRGBProfile()
            transform=None
            try:
                assert source and target, 'Independent profile opening failed'
                transform=lib.cmsCreateTransform(source,(1<<22)|(space<<16)|(inputs<<3),
                    target,(1<<22)|(4<<16)|(3<<3),1,0x140)
                assert transform, 'Independent relative-colorimetric transform failed'
                points=list(itertools.product((0,1),repeat=inputs));randomizer=random.Random(193)
                points += [[randomizer.random() for _ in range(inputs)] for _ in range(args.samples-len(points))]
                maximum=0
                for point in points:
                    incoming=(c.c_double*inputs)(*(v*(100 if inputs==4 else 1) for v in point))
                    outgoing=(c.c_double*3)();lib.cmsDoTransform(transform,incoming,outgoing,1)
                    expected=[int(max(0,min(1,v))*255+.5) for v in outgoing]
                    pdf.write_bytes(fixture(raw,point,inputs))
                    result=subprocess.run([str(args.probe.resolve()),str(pdf),'0'],capture_output=True,timeout=30)
                    assert result.returncode==0 and len(result.stdout)==12+24*16*4,(profile,result.stderr)
                    actual=list(result.stdout[12:15]);error=max(abs(a-b) for a,b in zip(actual,expected))
                    assert error<=2,(profile,point,actual,expected,error)
                    maximum=max(maximum,error)
                rows.append({'profile':str(profile),'sha256':hashlib.sha256(raw).hexdigest(),
                             'samples':len(points),'maximum_channel_difference':maximum,
                             'black_point_compensation':False})
            finally:
                if transform:lib.cmsDeleteTransform(transform)
                if source:lib.cmsCloseProfile(source)
                if target:lib.cmsCloseProfile(target)
    print(json.dumps(rows,indent=2))


if __name__=='__main__':main()
