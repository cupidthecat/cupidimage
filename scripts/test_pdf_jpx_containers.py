#!/usr/bin/env python3
"""Check JPEG 2000 boxes, palettes, channels and in-file fragments."""
import base64
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


ROWS=json.loads(Path('tests/fixtures/pdf-jpx-samples.json').read_text())['cases']


def box(kind,data,wide=False,to_end=False):
    return (struct.pack('>I4sQ',1,kind,len(data)+16) if wide else
            struct.pack('>I4s',0 if to_end else len(data)+8,kind))+data


def signature(extended=False):
    brand=b'jpx ' if extended else b'jp2 '
    return box(b'jP  ',b'\r\n\x87\n')+box(b'ftyp',brand+bytes(4)+(b'jpxb' if extended else b'jp2 '))


def ihdr(row,bits=None,width=None):
    return box(b'ihdr',struct.pack('>IIHBBBB',row['height'],row['width'] if width is None else width,
                                  row['components'],(row['precision']-1+128*row['signed']) if bits is None else bits,7,0,0))


def colr(enumeration=16,precedence=0,approximation=0,wide=False):
    return box(b'colr',struct.pack('>BbBI',1,precedence,approximation,enumeration),wide=wide)


def cdef(definitions):
    return box(b'cdef',struct.pack('>H',len(definitions))+b''.join(struct.pack('>HHH',*d) for d in definitions))


def palette(values,bits):
    payload=struct.pack('>HB',len(values),len(bits))+bytes(bits)
    for row in values:
        for value,b in zip(row,bits):
            precision=(b&127)+1
            payload+=(value&((1<<precision)-1)).to_bytes((precision+7)//8,'big')
    return box(b'pclr',payload)


def cmap(mapping):
    return box(b'cmap',b''.join(struct.pack('>HBB',*m) for m in mapping))


def jp2(row,extra=b'',extended=False,layer=b'',stream_header=b'',wide=False,to_end=False):
    color=colr(17 if row['components']==1 else 12 if row['components']==4 else 16,wide=wide)
    return signature(extended)+box(b'jp2h',ihdr(row)+color+extra,wide=wide)+stream_header+layer+box(b'jp2c',base64.b64decode(row['codestream']),wide=wide,to_end=to_end)


def source_values(row):
    data=zlib.decompress(base64.b64decode(row['samples_u32be_zlib']))
    values=list(struct.unpack('>'+str(len(data)//4)+'I',data));count=row['width']*row['height']
    return [values[c*count:(c+1)*count] for c in range(row['components'])]


def fragment_list(fragments):
    return box(b'flst',struct.pack('>H',len(fragments))+b''.join(struct.pack('>QIH',*f) for f in fragments))


def fragmented(row,shared=False,zeros=False):
    raw=base64.b64decode(row['codestream']);cut=len(raw)//2
    prefix=signature(True)+box(b'jp2h',ihdr(row)+colr())
    if shared:
        color=colr(16,7,1);first=len(prefix)+8
        prefix+=box(b'free',color[:5]);second=len(prefix)+8
        prefix+=box(b'free',color[5:])
        fragments=[(first,5,0),(second,len(color)-5,0)]
        if zeros:fragments.insert(0,(12,0,0));fragments.append((len(prefix),0,0))
        prefix+=box(b'jplh',box(b'cref',b'cgrp'+fragment_list(fragments)))
    count=4 if zeros else 2;table_length=8+8+2+14*count
    first=len(prefix)+table_length+8;second=first+cut+8
    fragments=[(first,cut,0),(second,len(raw)-cut,0)]
    if zeros:fragments.insert(0,(12,0,0));fragments.append((second+len(raw)-cut,0,0))
    return prefix+box(b'ftbl',fragment_list(fragments))+box(b'mdat',raw[:cut])+box(b'mdat',raw[cut:])


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupid-jpx-boxes-') as directory:
        path=Path(directory)/'case.jp2'
        def run(data,flags=()):
            nonlocal checks
            path.write_bytes(data);r=subprocess.run([probe,str(path),*flags],capture_output=True,text=True,timeout=30)
            assert r.returncode==0,(flags,r.returncode,r.stdout[:500],r.stderr)
            checks+=1
            return r.stdout
        def read(data):
            lines=run(data).splitlines();head=list(map(int,lines.pop(0).split()));colors=[];planes=[];types=[];keys=[]
            for _ in range(head[3]):colors.append(tuple(map(int,lines.pop(0).split()[1:])))
            if head[5]:keys=list(map(int,lines.pop(0).split()[1:]))
            for _ in range(head[2]):
                types.append(tuple(map(int,lines.pop(0).split()[1:])))
                planes.append(list(map(int,lines.pop(0).split())))
            spaces=[tuple(map(int,line.split()[1:])) for line in lines]
            return head,colors,planes,types,spaces,keys
        def fault(data,reject=False):
            nonlocal allocations
            allocations+=int(run(data,['--faults',*(['--reject'] if reject else [])]).split()[0])
        for row in ROWS:
            for wide,to_end in ((False,False),(True,False),(False,True)):
                data=jp2(row,wide=wide,to_end=to_end);head,colors,planes,types,spaces,keys=read(data)
                assert head[:6]==[row['width'],row['height'],row['components'],row['components'],0,0]
                assert planes==source_values(row) and types==[(row['precision'],row['signed'])]*row['components'],row['name']
                assert colors==[(c,65535,0) for c in range(row['components'])]
                if row['name']=='RGB-unsigned' and not wide and not to_end:fault(data)
        gray=next(r for r in ROWS if r['name']=='L-unsigned')
        rgb=next(r for r in ROWS if r['name']=='RGB-unsigned')
        rgba=next(r for r in ROWS if r['name']=='RGBA-unsigned')
        indices=source_values(gray)[0]
        values=[(i,255-i,i^85) for i in range(256)]
        extra=palette(values,[7,7,7])+cmap([(0,1,2),(0,1,0),(0,1,1)])
        data=jp2(gray,extra=extra);head,colors,planes,types,spaces,keys=read(data)
        assert head[2:4]==[3,3] and planes==[[values[i][c] for i in indices] for c in (2,0,1)]
        fault(data)
        # Direct and palette channels can share one component without losing precision.
        bits=[0,11,15,30,37,128|11]
        values=[(i&1,i*16,i*257,i*65537,(1<<37)+i,-2048+i) for i in range(256)]
        mapping=[(0,1,c) for c in range(len(bits))]+[(0,0,0)]
        data=jp2(gray,extra=palette(values,bits)+cmap(mapping));head,colors,planes,types,spaces,keys=read(data)
        want=[[values[i][c]+(2048 if c==5 else 0) for i in indices] for c in range(len(bits))]+[indices]
        assert planes==want and types==[(1,0),(12,0),(16,0),(31,0),(38,0),(12,1),(8,0)]
        fault(data)
        for alpha_type in (1,2):
            defs=[(2,0,1),(0,0,2),(1,0,3),(3,alpha_type,0)]
            data=jp2(rgba,extra=cdef(defs));head,colors,planes,types,spaces,keys=read(data)
            assert head[2:6]==[4,3,1,0] and colors==[(2,3,alpha_type),(0,3,alpha_type),(1,3,alpha_type)]
            assert planes==source_values(rgba);fault(data)
            # The same opacity channel may list its colour associations separately.
            defs=defs[:3]+[(3,alpha_type,c) for c in (1,2,3)]
            assert read(jp2(rgba,extra=cdef(defs)))[1]==colors
        # Auxiliary, unassociated channels are not counted as ordinary colors.
        defs=[(0,0,1),(1,65535,65535),(2,0,2),(3,0,65535)]
        assert read(jp2(rgba,extra=cdef(defs)))[0][2:4]==[4,2]
        for opacity in (0,1):
            data=jp2(rgba,extended=True,layer=box(b'jplh',box(b'opct',bytes([opacity]))))
            head,colors,planes,types,spaces,keys=read(data)
            assert head[2:6]==[4,3,1,0] and colors==[(c,3,opacity+1) for c in range(3)]
            fault(data)
        key=[p[0] for p in source_values(rgb)]
        data=jp2(rgb,extended=True,layer=box(b'jplh',box(b'opct',bytes([2,3,*key]))))
        assert read(data)[5]==key;fault(data)
        colors=box(b'cgrp',colr(16,-3,3)+colr(16,7,1))
        data=jp2(rgb,extended=True,layer=box(b'jplh',colors+box(b'creg',struct.pack('>HHHBBBB',2,3,0,2,3,0,0))))
        assert read(data)[4]==[(1,-3,3,16,0),(1,7,1,16,0)];fault(data)
        # First codestream/header selection and inherited defaults are explicit.
        prefix=signature(True)+box(b'jp2h',ihdr(rgb,width=rgb['width']+1)+colr())
        data=prefix+box(b'jpch',ihdr(rgb))+box(b'jpch',ihdr(gray))+box(b'jp2c',base64.b64decode(rgb['codestream']))+box(b'jp2c',b'not used')
        assert read(data)[2]==source_values(rgb)
        for shared,zeros in ((False,False),(True,False),(False,True),(True,True)):
            data=fragmented(rgb,shared,zeros);head,colors,planes,types,spaces,keys=read(data)
            assert planes==source_values(rgb)
            if shared:assert spaces==[(1,7,1,16,0)]
            fault(data)
        valid=jp2(rgb)
        for length in (0,1,7,11,12,19,len(valid)-1):assert not run(valid[:length],['--reject'])
        invalid=[b'\0\0\0\1jP  '+bytes(8),signature()+box(b'jp2h',colr()+ihdr(rgb))+box(b'jp2c',base64.b64decode(rgb['codestream'])),
                 jp2(gray,extra=palette([(0,0,0)],[7,7,7])),jp2(gray,extra=cmap([(0,0,0)])),
                 jp2(rgb,extra=cdef([(0,0,1),(1,0,1),(2,0,3)])),
                 jp2(rgb,extra=cdef([(0,0,1),(1,0,2),(3,0,3)])),
                 jp2(rgba,extra=cdef([(0,0,1),(1,0,2),(2,0,3),(3,3,0)])),
                 jp2(rgba,extra=cdef([(0,0,1),(1,0,2),(2,0,3),(3,1,0),(3,1,1)])),
                 jp2(rgb,extra=box(b'free',b'bad',to_end=True)),
                 jp2(rgb,extended=True,layer=box(b'jplh',box(b'creg',struct.pack('>HHHBBBB',2,3,0,1,3,0,0)))),
                 jp2(rgb,extended=True,layer=box(b'jplh',box(b'cref',b'cref'+fragment_list([(12,4,0)]))))]
        for data in invalid:assert not run(data,['--reject']);fault(data,True)
        # External fragment resources are not opened, even when a path-like box exists.
        data=signature(True)+box(b'jp2h',ihdr(rgb)+colr())+box(b'ftbl',fragment_list([(12,10,1)]))
        assert not run(data,['--reject']);fault(data,True)
    print(f'JPEG 2000 containers: {checks} checks; {allocations} allocation checks')


if __name__=='__main__':main()
