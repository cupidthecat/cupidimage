#!/usr/bin/env python3
"""Check JPEG 2000 sample precision, component planes and bounded failures."""
import base64
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=allocations=0
    subprocess.run([probe,'--allocator'],check=True,timeout=20)
    rows=json.loads(Path('tests/fixtures/pdf-jpx-samples.json').read_text())['cases']
    with tempfile.TemporaryDirectory(prefix='cupid-jpx-') as directory:
        path=Path(directory)/'case.j2k'
        def run(data,flags=()):
            nonlocal checks
            path.write_bytes(data);r=subprocess.run([probe,str(path),*flags],capture_output=True,text=True,timeout=30)
            assert r.returncode==0,(flags,r.returncode,r.stdout,r.stderr)
            checks+=1
            return r.stdout
        for row in rows:
            data=base64.b64decode(row['codestream']);lines=run(data).splitlines()
            width,height,components,x0,y0,work=map(int,lines[0].split())
            assert (width,height,components,x0,y0)==(row['width'],row['height'],row['components'],*row['origin']),row['name']
            samples=[]
            for c in range(components):
                assert list(map(int,lines[1+2*c].split()[1:]))==[width,height,1,1,x0,y0,row['precision'],row['signed']],row['name']
                samples.extend(map(int,lines[2+2*c].split()))
            expected=zlib.decompress(base64.b64decode(row['samples_u32be_zlib']))
            assert struct.pack('>'+str(len(samples))+'I',*samples)==expected,row['name']
            if width==3:
                allocations+=int(run(data,['--faults']).split()[0])
            assert not run(data,['--work=0','--reject'])
            assert not run(data,[f'--work={256*1024*1024-work-1}','--reject'])
        basic=base64.b64decode(rows[0]['codestream'])
        for n in range(len(basic)):
            assert not run(basic[:n],['--reject']),n
        bad=[]
        for offset,value,fmt in ((8,0,'>I'),(8,0xffffffff,'>I'),(12,0xffffffff,'>I'),(40,33,'>H'),(42,0xff,'>B')):
            data=bytearray(basic);struct.pack_into(fmt,data,offset,value);bad.append(bytes(data))
        # A TLM declaration cannot reserve one tile-part and then supply two.
        start=basic.index(b'\xff\x90')
        first=bytearray(basic[start:-2]);first[11]=0
        length=struct.unpack_from('>I',first,6)[0]
        tlm=b'\xff\x55'+struct.pack('>HBBBI',9,0,0x50,0,length)
        extra=b'\xff\x90'+struct.pack('>HHIBB',10,0,14,1,0)+b'\xff\x93'
        bad.append(basic[:start]+tlm+first+extra+b'\xff\xd9')
        for data in bad:
            assert not run(data,['--reject'])
            allocations+=int(run(data,['--faults','--reject']).split()[0])
    print(f'JPEG 2000 samples: {checks} checks; {allocations} allocation checks')


if __name__=='__main__':main()
