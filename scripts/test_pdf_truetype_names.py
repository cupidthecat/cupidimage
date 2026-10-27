#!/usr/bin/env python3
"""Verify all standard post names, signed legacy offsets, and custom name bounds."""
import base64
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_truetype import tables,replace_tables


def expanded(font):
    records=tables(font)
    def get(tag):
        _,start,length=records[tag];return font[start:start+length]
    maxp=bytearray(get(b'maxp'));old=struct.unpack_from('>H',maxp,4)[0];struct.pack_into('>H',maxp,4,258)
    loca=get(b'loca');assert struct.unpack_from('>H',get(b'head'),50)[0]==0
    return replace_tables(font,{b'maxp':bytes(maxp),b'loca':loca+loca[-2:]*(258-old),b'hmtx':get(b'hmtx')+bytes((258-old)*2)})


def main():
    probe=str(Path(sys.argv[1]).resolve());font=base64.b64decode(json.loads(Path('tests/fixtures/pdf-truetype.json').read_text())['font']);wide=expanded(font)
    names=json.loads(Path('tests/fixtures/pdf-post-names.json').read_text())['names']
    count=references=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-tt-post-') as tmp:
        path=Path(tmp)/'font.ttf'
        def check(table,queries,want,source=wide,compare=False):
            nonlocal count,references
            path.write_bytes(replace_tables(source,{b'post':table}))
            result=subprocess.run([probe,str(path),'post-list',*queries],capture_output=True,text=True,timeout=10)
            assert result.returncode==0,result.stderr
            actual=list(map(int,result.stdout.split()))
            assert actual==want,(queries,actual,want)
            if compare and '--reference' in sys.argv:
                if table[:4]==b'\0\x02\x50\0':
                    import ctypes
                    import ctypes.util
                    lib=ctypes.CDLL(ctypes.util.find_library('freetype'))
                    lib.FT_Init_FreeType.argtypes=[ctypes.POINTER(ctypes.c_void_p)]
                    lib.FT_New_Memory_Face.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_long,ctypes.c_long,ctypes.POINTER(ctypes.c_void_p)]
                    lib.FT_Get_Name_Index.argtypes=[ctypes.c_void_p,ctypes.c_char_p]
                    lib.FT_Get_Name_Index.restype=ctypes.c_uint
                    lib.FT_Done_Face.argtypes=[ctypes.c_void_p]
                    lib.FT_Done_FreeType.argtypes=[ctypes.c_void_p]
                    library=ctypes.c_void_p();face=ctypes.c_void_p()
                    assert lib.FT_Init_FreeType(ctypes.byref(library))==0
                    raw=path.read_bytes();buffer=ctypes.create_string_buffer(raw)
                    try:
                        assert lib.FT_New_Memory_Face(library,buffer,len(raw),0,ctypes.byref(face))==0
                        assert [lib.FT_Get_Name_Index(face,name.encode()) for name in queries]==actual
                    finally:
                        if face:lib.FT_Done_Face(face)
                        lib.FT_Done_FreeType(library)
                else:
                    from fontTools.ttLib import TTFont
                    with TTFont(path) as independent:
                        order=independent.getGlyphOrder()
                    indices={name:i for i,name in enumerate(order)}
                    assert [indices.get(name,0) for name in queries]==actual
                references+=len(queries)
            count+=len(queries)
        header=lambda version:struct.pack('>I',version)+bytes(28)
        check(header(0x10000),names,list(range(258)),compare=True)
        check(header(0x10000),['A'],[0],source=font)
        check(header(0x20000)+struct.pack('>H',258)+struct.pack('>258H',0,*reversed(range(1,258))),names,[0,*reversed(range(1,258))],compare=True)
        order=list(range(258));order[100],order[120]=order[120],order[100]
        offsets=bytes((v-i)&255 for i,v in enumerate(order))
        check(header(0x25000)+struct.pack('>H',258)+offsets,names,[order.index(i) for i in range(258)],compare=True)
        for at,value in [(34,255),(34+257,1)]:
            bad=bytearray(header(0x25000)+struct.pack('>H',258)+offsets);bad[at]=value
            check(bytes(bad),['A'],[0])
        check(header(0x25000)+struct.pack('>H',258)+offsets[:-1],['A'],[0])
        for version in (0,0x30000,0x28000,0x40000):check(header(version),['A','Delta'],[0,0])
        ids=[0,258,259,260]+[0]*254
        table=header(0x20000)+struct.pack('>H',258)+struct.pack('>258H',*ids)+b'\x06custom\x04Euro\x04long'
        check(table,['custom','Euro','long','absent'],[1,2,3,0])
        check(table[:-2],['long'],[0])
        for length in (0,3,31,32,33,35,100,548):check(table[:length],['custom'],[0])
        ids[1]=32768
        check(header(0x20000)+struct.pack('>H',258)+struct.pack('>258H',*ids)+b'\x06custom',['custom'],[0])
        check(header(0x20000)+struct.pack('>H',257)+struct.pack('>258H',*ids),['A'],[0])
    print(f'TrueType post names: {count} standard, custom and malformed-table checks passed; {references} independent name mappings')

if __name__=='__main__':main()
