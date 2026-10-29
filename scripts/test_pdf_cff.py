#!/usr/bin/env python3
"""Exercise bounded CFF structures with authored byte-level fixtures."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def integer(n):
    return b'\x1d'+struct.pack('>i',n)


def real(n):
    value=str(n).upper().replace('E+','E')
    nibbles=[]
    for c in value:
        nibbles.append(int(c) if c.isdigit() else {'.':10,'E':11,'-':14}[c])
    nibbles.append(15)
    if len(nibbles)%2:nibbles.append(15)
    return b'\x1e'+bytes(nibbles[i]*16+nibbles[i+1] for i in range(0,len(nibbles),2))


def index(items):
    if not items:return b'\0\0'
    offsets=[1]
    for item in items:offsets.append(offsets[-1]+len(item))
    return struct.pack('>H',len(items))+b'\x04'+b''.join(struct.pack('>I',n) for n in offsets)+b''.join(items)


def fixture(charset=None,encoding=None,glyphs=4,strings=(b'customShape',),cid=False,fdselect=None,private_count=1,subrs=True,programs=None,local_subrs=None,global_programs=(),matrix=None,private_prefixes=None,fd_matrices=None):
    if charset is None:charset=b'\0\0\1\0\x22\x01\x87'
    if encoding is None:encoding=0
    parts={}
    if isinstance(charset,bytes):parts['charset']=charset
    if isinstance(encoding,bytes):parts['encoding']=encoding
    parts['chars']=index(programs if programs is not None else [b'\x0e']*glyphs)
    for i in range(private_count):
        # Subrs offset is relative to the beginning of its own Private DICT.
        private=integer(600+i)+b'\x14'+integer(100+i)+b'\x15'
        if private_prefixes is not None:private=private_prefixes[i]+private
        if subrs:private+=integer(len(private)+6)+b'\x13'
        parts[f'private{i}']=private
        if subrs:parts[f'subrs{i}']=index(local_subrs if local_subrs is not None else [b'\x0b'])
    if cid:
        parts['fds']=b''
        parts['fdselect']=fdselect if fdselect is not None else b'\0'+bytes(glyphs)
    names=index([b'CupidShapes']);string_index=index(list(strings));global_subrs=index(global_programs)
    positions={key:0 for key in parts}
    def fontdict(i):
        prefix=b''.join(real(n) for n in fd_matrices[i])+bytes([12,7]) if fd_matrices is not None and fd_matrices[i] is not None else b''
        return prefix+integer(len(parts[f'private{i}']))+integer(positions[f'private{i}'])+b'\x12'
    def top():
        data=(b''.join(real(n) for n in matrix)+bytes([12,7])) if matrix is not None else b''
        data+=integer(positions['charset'] if isinstance(charset,bytes) else charset)+b'\x0f'
        data+=integer(positions['chars'])+b'\x11'
        if cid:
            data+=b'\x8b\x8b\x8b\x0c\x1e'+integer(positions['fds'])+b'\x0c\x24'+integer(positions['fdselect'])+b'\x0c\x25'
        else:data+=integer(positions['encoding'] if isinstance(encoding,bytes) else encoding)+b'\x10'+fontdict(0)
        return data
    if cid:parts['fds']=index([fontdict(i) for i in range(private_count)])
    prefix=b'\1\0\4\4'+names+index([top()])+string_index+global_subrs
    cursor=len(prefix)
    for key,value in parts.items():positions[key]=cursor;cursor+=len(value)
    if cid:parts['fds']=index([fontdict(i) for i in range(private_count)])
    prefix=b'\1\0\4\4'+names+index([top()])+string_index+global_subrs
    return prefix+b''.join(parts.values())


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-cff-') as tmp:
        path=Path(tmp)/'font.cff'
        def run(data,ok=True,contains=()):
            nonlocal checks
            path.write_bytes(data);result=subprocess.run([probe,str(path)],capture_output=True,text=True,timeout=10)
            assert result.returncode==(0 if ok else 2),(result.returncode,result.stderr,result.stdout)
            for text in contains:assert text in result.stdout,(text,result.stdout)
            checks+=1
        basic=fixture()
        run(basic,contains=('4 0 1','names 1 2 3','code 32 1','code 65 2','private 0 600 100 1'))
        run(fixture(charset=0,strings=()),contains=('code 32 1','code 33 2','code 34 3'))
        run(fixture(charset=1,strings=(),encoding=1),contains=('code 32 1','code 33 2'))
        run(fixture(charset=2,strings=(),encoding=1),contains=('code 32 1',))
        run(fixture(charset=b'\1\0\1\0\0\x22\0\1\x87\0'))
        run(fixture(charset=b'\2\0\1\0\0\0\x22\0\0\1\x87\0\0'))
        run(fixture(encoding=b'\0\3\x20\x41\x42'),contains=('code 32 1','code 65 2','code 66 3'))
        run(fixture(encoding=b'\1\1\x41\2'),contains=('code 65 1','code 66 2','code 67 3'))
        run(fixture(encoding=b'\x80\2\x20\x41\1\x42\1\x87'),contains=('code 66 3',))
        run(fixture(cid=True,private_count=2,fdselect=b'\0\0\1\0\1'),contains=('4 1 2','fd 1 1','fd 2 0','private 1 601 101 1'))
        run(fixture(cid=True,private_count=2,fdselect=b'\3\0\2\0\0\0\0\2\1\0\4'),contains=('fd 0 0','fd 1 0','fd 2 1','fd 3 1'))
        for bad in (b'\0\0\1\0\1\1\x87',b'\3\0\1',b'\1\xff\xff\2',b'\1\0\1\4'):
            run(fixture(charset=bad),False)
        for bad in (b'\2\0',b'\0\2\x41\x41',b'\1\1\xff\2',b'\0\4\1\2\3\4',b'\x80\0\1\x41\xff\xff'):
            run(fixture(encoding=bad),False)
        for bad in (b'\0\0\2\0\0',b'\3\0\0',b'\3\0\1\0\1\0\0\4',b'\3\0\1\0\0\0\0\3',b'\3\0\1\0\0\2\0\4'):
            run(fixture(cid=True,private_count=2,fdselect=bad),False)
        run(fixture(cid=True,charset=0),False)
        run(fixture(charset=0,glyphs=230),False)
        for cut in range(len(basic)):
            run(basic[:cut],False)
        for offset,value in ((0,2),(1,1),(2,3),(3,0),(3,5)):
            broken=bytearray(basic);broken[offset]=value;run(broken,False)
    print(f'CFF structures: {checks} checks passed')


if __name__=='__main__':main()
