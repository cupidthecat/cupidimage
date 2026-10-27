#!/usr/bin/env python3
"""Exercise mixed-width and full-repertoire TrueType cmap tables and their bounds."""
import base64
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_truetype import replace_tables


def format2():
    keys=[0]*256
    keys[0x81]=8;keys[0x82]=16
    # The second two-byte subheader shares glyph data but applies a different delta.
    subheaders=struct.pack('>4H',65,3,0,18)+struct.pack('>4H',0x40,3,65535,16)+struct.pack('>4H',0x40,3,1,8)
    body=struct.pack('>256H',*keys)+subheaders+struct.pack('>6H',1,0,2,2,0,3)
    return struct.pack('>3H',2,6+len(body),0)+body


def format8():
    bits=bytearray(8192)
    for value in (0,1):bits[value//8]|=1<<(7-value%8)
    groups=struct.pack('>6I',65,66,1,0x10000,0x10001,3)
    return struct.pack('>HHII',8,0,8208+len(groups),0)+bits+struct.pack('>I',2)+groups


def format10(start=0x10000,values=(1,0,3)):
    return struct.pack('>HHIIII',10,0,20+2*len(values),0,start,len(values))+struct.pack('>'+str(len(values))+'H',*values)


def main():
    probe=str(Path(sys.argv[1]).resolve());font=base64.b64decode(json.loads(Path('tests/fixtures/pdf-truetype.json').read_text())['font'])
    checks=references=0
    reference=None
    if '--reference' in sys.argv:
        import ctypes
        import ctypes.util
        reference=ctypes.CDLL(ctypes.util.find_library('freetype'))
        reference.FT_Init_FreeType.argtypes=[ctypes.POINTER(ctypes.c_void_p)]
        reference.FT_New_Memory_Face.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_long,ctypes.c_long,ctypes.POINTER(ctypes.c_void_p)]
        reference.FT_Select_Charmap.argtypes=[ctypes.c_void_p,ctypes.c_uint]
        reference.FT_Get_Char_Index.argtypes=[ctypes.c_void_p,ctypes.c_ulong]
        reference.FT_Get_Char_Index.restype=ctypes.c_uint
        reference.FT_Done_Face.argtypes=[ctypes.c_void_p]
        reference.FT_Done_FreeType.argtypes=[ctypes.c_void_p]
        library=ctypes.c_void_p()
        assert reference.FT_Init_FreeType(ctypes.byref(library))==0

    with tempfile.TemporaryDirectory(prefix='cupidimage-tt-cmaps-') as tmp:
        path=Path(tmp)/'font.ttf'
        def check(table,cases,compare=False,header_version=0):
            nonlocal checks,references
            cmap=struct.pack('>HHHHI',header_version,1,3,10,12)+table
            path.write_bytes(replace_tables(font,{b'cmap':cmap}))
            result=subprocess.run([probe,str(path),'map-list','3','10',*[str(c) for c,g in cases]],capture_output=True,text=True,timeout=10)
            assert result.returncode==0,result.stderr
            actual=list(map(int,result.stdout.split()));expected=[g for c,g in cases]
            assert actual==expected,(table[:20].hex(),cases,actual)
            if reference and compare:
                raw=path.read_bytes();buffer=ctypes.create_string_buffer(raw);face=ctypes.c_void_p()
                assert reference.FT_New_Memory_Face(library,buffer,len(raw),0,ctypes.byref(face))==0
                try:
                    assert reference.FT_Select_Charmap(face,0x756e6963)==0
                    independent=[reference.FT_Get_Char_Index(face,c) for c,g in cases]
                    assert actual==independent,(table[:20].hex(),cases,actual,independent)
                    references+=len(cases)
                finally:reference.FT_Done_Face(face)
            checks+=len(cases)
        table=format2()
        check(table,[(0,0),(64,0),(65,1),(66,0),(67,2),(68,0),(0x81,0),(0x82,0),(0x80ff,0),(0x813f,0),(0x8140,1),(0x8141,0),(0x8142,2),(0x8143,0),(0x8240,3),(0x8241,0),(0x8242,4),(0xffff,0),(0x10000,0)],compare=True)
        for at,value in [(6+0x81*2,7),(6+0x82*2,65528),(518,256),(520,256),(524,0),(524,19),(524,65534),(532,0),(532,65534)]:
            broken=bytearray(table);struct.pack_into('>H',broken,at,value)
            check(bytes(broken),[(0x8140 if at==532 else 65,0)])
        for length in (0,1,3,4,5,6,517,518,525,len(table)-1):check(table[:length],[(65,0),(0x8140,0)])
        table=format10()
        check(table,[(0,0),(65535,0),(65536,1),(65537,0),(65538,3),(65539,0),(0xffffffff,0)],compare=True)
        check(format10(0xfffffffe,(1,2)),[(0xfffffffd,0),(0xfffffffe,1),(0xffffffff,2)],compare=True)
        check(format10(0xfffffffe,(1,2,3)),[(0xfffffffe,0)])
        for at,value in [(4,19),(4,27),(16,0xffffffff)]:
            broken=bytearray(table);struct.pack_into('>I',broken,at,value);check(bytes(broken),[(65536,0)])
        for length in (0,4,7,8,12,16,19,20,25):check(table[:length],[(65536,0)])
        table=format8()
        check(table,[(0,0),(64,0),(65,1),(66,2),(67,0),(65535,0),(65536,3),(65537,4),(65538,0),(0xffffffff,0)],compare=True)
        for length in (0,4,7,8,16,8203,8204,8207,8208,len(table)-1):check(table[:length],[(65,0)])
        for at,value in [(4,8207),(4,len(table)+1),(8204,0xffffffff),(8216,0xffffffff)]:
            broken=bytearray(table);struct.pack_into('>I',broken,at,value);check(bytes(broken),[(65,0)])
        for table in (format8(),format10()):
            broken=bytearray(table);struct.pack_into('>H',broken,2,1);check(bytes(broken),[(65,0),(65536,0)])
        check(format10(),[(65536,0)],header_version=1)
    if reference:reference.FT_Done_FreeType(library)
    print(f'TrueType mixed/full cmap: {checks} mapping and malformed-boundary checks passed; {references} independent mappings')

if __name__=='__main__':main()
