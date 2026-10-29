#!/usr/bin/env python3
"""Verify authored binary Type 1 font programs and malformed boundaries."""
from pathlib import Path
import subprocess
import sys
import tempfile


def encrypt(data,key):
    result=bytearray()
    for value in data:
        cipher=value^(key>>8)
        result.append(cipher)
        key=((key+cipher)*52845+22719)&65535
    return bytes(result)


def fixture(glyphs=None,subrs=(b'\x0b',),len_iv=4,reader='RD',encoding='StandardEncoding',matrix='[.001 0 0 .001 0 0]',extra=b'',separator=b' ',iv_first=0,outer_encrypted=True):
    if glyphs is None:glyphs=[('.notdef',b'\x8b\xf8\xec\x0d\x0e'),('A',b'\x8b\xf8\xec\x0d\x8b\x8b\x15\x0e')]
    clear=('%!PS-AdobeFont-1.0: CupidShapes\n11 dict begin\n/FontName /CupidShapes def\n/FontType 1 def\n/PaintType 0 def\n/FontBBox [0 -200 1000 1000] readonly def\n/FontMatrix '+matrix+' readonly def\n/Encoding '+encoding+' def\ncurrentdict end\ncurrentfile eexec\n').encode()
    body=b'dup /Private 10 dict dup begin\n/'+reader.encode()+b' {string currentfile exch readstring pop} executeonly def\n/ND {noaccess def} executeonly def\n/NP {noaccess put} executeonly def\n/password 5839 def\n/MinFeature {16 16} def\n/lenIV '+str(len_iv).encode()+b' def\n'+extra
    def encrypted(program):return program if len_iv<0 else encrypt((bytes([iv_first])+bytes(range(1,len_iv)) if len_iv else b'')+program,4330)
    body+=b'/Subrs '+str(len(subrs)).encode()+b' array\n'
    for i,program in enumerate(subrs):
        payload=encrypted(program);body+=f'dup {i} {len(payload)} {reader}'.encode()+separator+payload+b' NP\n'
    body+=b'ND\n2 index /CharStrings '+str(len(glyphs)).encode()+b' dict dup begin\n'
    for name,program in glyphs:
        payload=encrypted(program);body+=f'/{name} {len(payload)} {reader}'.encode()+separator+payload+b' ND\n'
    body+=b'end end readonly put noaccess put dup /FontName get exch definefont pop mark currentfile closefile\n'
    if not outer_encrypted:
        data=clear.replace(b'currentfile eexec\n',b'')+body
        return data,len(data),0
    encrypted_body=encrypt(b'\x00\x01\x02\x03'+body,55665)
    return clear+encrypted_body,len(clear),len(encrypted_body)


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-type1-') as tmp:
        path=Path(tmp)/'font.pfa'
        def run(parts,ok=True,contains=()):
            nonlocal checks
            data,l1,l2=parts;path.write_bytes(data)
            result=subprocess.run([probe,str(path),str(l1),str(l2)],capture_output=True,text=True,timeout=10)
            assert result.returncode==(0 if ok else 2),(result.returncode,result.stderr,result.stdout)
            for text in contains:assert text in result.stdout,(text,result.stdout)
            checks+=1
        run(fixture(matrix='[1e-3 0 0 1e-3 16#10 8#10]'),contains=('matrix 0.001 0 0 0.001 16 8',))
        run(fixture(matrix='[.001 0 0 .001 16#FFFFFFFF 16#80000000]'),contains=('matrix 0.001 0 0 0.001 -1 -2.14748365e+09',))
        raw,l1,l2=fixture()
        comment=b'% ignored comment\f'
        raw=raw[:l1].replace(b'/FontType',comment+b'/FontType')+raw[l1:]
        run((raw,l1+len(comment),l2),contains=('2 1 4',))
        run(fixture(extra=b'/Ignored <~!!>!!~> def\n/Unused << /A 1 >> def\n'),contains=('2 1 4',))
        for matrix in ('[0x1p-10 0 0 .001 0 0]', '[.001 0 0 .001 16#100000000 0]'):
            run(fixture(matrix=matrix),False)
        run(fixture(outer_encrypted=False),contains=('glyph 0 .notdef 8bf8ec0d0e','subr 0 0b'))
        for iv in (-1,0,1,4,16):run(fixture(len_iv=iv),contains=('code 65 1','glyph 0 .notdef 8bf8ec0d0e','subr 0 0b'))
        run(fixture(separator=b'\r',iv_first=26),contains=('glyph 0 .notdef 8bf8ec0d0e',))
        for reader in ('RD','-|','ReadBinary'):run(fixture(reader=reader),contains=('code 65 1',))
        run(fixture(encoding='256 array 0 1 255 {1 index exch /.notdef put} for dup 66 /A put'),contains=('code 66 1',))
        run(fixture(encoding='StandardEncoding 256 array copy dup 66 /A put'),contains=('code 65 1','code 66 1'))
        run(fixture(matrix='[0 .002 -.001 0 .1 -.2]'),contains=('matrix 0 0.002 -0.001 0 0.1 -0.2',))
        run(fixture(glyphs=[('!mark',b'\x0e'),('.notdef',b'\x0e'),('A',b'\x0e')],encoding='StandardEncoding 256 array copy dup 66 /!mark put'),contains=('code 65 2','code 66 1','glyph 0 .notdef'))
        run(fixture(glyphs=[('A',b'\x0e'),('.notdef',b'\x0e')]),contains=('glyph 0 .notdef 0e','code 65 1'))
        run(fixture(glyphs=[('.notdef',b'\x0e'),('A',b'\x0e'),('A',b'\x8b\x0e')]),contains=('2 1 4','glyph 1 A 8b0e'))
        run(fixture(extra=b'/Ignored {(unclosed-ish\\) string) pop /lenIV 30 def} def\n'),contains=('2 1 4',))
        run(fixture(extra=b'/Subrs 1 array def\n'),contains=('2 1 4','subr 0 0b'))
        old=encrypt(b'\0\1\2\3\x0e',4330)
        run(fixture(extra=b'/CharStrings 1 dict dup begin /OldOnly 5 RD '+old+b' ND end def\n'),contains=('2 1 4',))
        for matrix in ('[0 0]','[1 0 0 1 0 0 2]','[1 0 0 1 nan 0]','[1 0 0 1 1e100 0]'):run(fixture(matrix=matrix),False)
        run(fixture(len_iv=-2),False)
        run(fixture(glyphs=[('A',b'\x0e')]),False)
        run(fixture(encoding='255 array'),False)
        large=[('.notdef',b'\x0e')]+[(f'g{i:05d}',b'\x0e') for i in range(10000)]
        run(fixture(glyphs=large,encoding='256 array dup 65 /g09999 put'),contains=('10001 1 4','code 65 10000'))
        raw,l1,l2=fixture()
        for size in (0,1,l1-1,l1,l1+3,len(raw)-1):run((raw[:size],l1,l2),False)
        run((raw,l1,len(raw)),False)
        run((raw,len(raw)+1,l2),False)
        for wrong in (0,1,2,3,l2//2):run((raw,l1,wrong),False)
    print(f'Type 1 font structures: {checks} checks passed')


if __name__=='__main__':main()
