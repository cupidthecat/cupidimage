#!/usr/bin/env python3
"""Check Type 2 paths, widths, stacks, subroutines and malformed programs."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_cff import fixture


def number(n):
    if int(n)!=n:return b'\xff'+struct.pack('>i',round(n*65536))
    n=int(n)
    if -107<=n<=107:return bytes([n+139])
    if 108<=n<=1131:return bytes([247+(n-108)//256,(n-108)%256])
    if -1131<=n<=-108:return bytes([251+(-n-108)//256,(-n-108)%256])
    return b'\x1c'+struct.pack('>h',n)


def command(values,op):
    return b''.join(number(n) for n in values)+bytes([op] if isinstance(op,int) else op)


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-type2-') as tmp:
        path=Path(tmp)/'font.cff'
        def render(code,ok=True,**kwargs):
            nonlocal checks
            path.write_bytes(fixture(charset=0,strings=(),glyphs=2,programs=[b'\x0e',code],**kwargs))
            result=subprocess.run([probe,str(path),'--outline'],capture_output=True,text=True,timeout=15)
            assert result.returncode==(0 if ok else 3),(code[:50],result.returncode,result.stderr,result.stdout)
            checks+=1
            if not ok:return None
            lines=result.stdout.splitlines();at=next(i for i,v in enumerate(lines) if v.startswith('outline 1 '))
            header=lines[at].split();segments=[tuple(map(float,v.split()[1:])) for v in lines[at+1:]]
            return float(header[2]),segments,header[4:]
        move=command([0,0],21);end=b'\x0e'
        def same(a,b,**kwargs):
            x=render(a,**kwargs);y=render(b,**kwargs);assert x==y,(a,b,x,y)
        baseline=move+command([10,0,0,20,-10,0],5)+end
        same(move+command([10,20,-10],6)+end,baseline)
        same(move+command([20,10,-20],7)+end,move+command([0,20,10,0,0,-20],5)+end)
        same(command([10],22)+command([20],4)+end,command([10,0],21)+command([0,20],21)+end)
        assert render(baseline)[0]==600
        assert render(command([700,0,0],21)+end)[0]==800
        assert render(command([700],14))[0]==800
        same(command([700,0,10],1)+move+end,command([700,0,0],21)+end)
        for stem in (1,3,18,23):
            same(command([0,10],stem)+move+end,move+end)
        same(bytes([12,0])+move+end,move+end)
        same(command([700],20)+move+end,command([700,0,0],21)+end)
        for mask in (19,20):
            same(command([0,10],1)+bytes([mask,0x80])+move+end,move+end)
        same(command([0,10],1)+command([20,30],19)+b'\xc0'+move+end,move+end)
        curves=[([1,2,3,4],26,[0,1,2,3,0,4]),([9,1,2,3,4],26,[9,1,2,3,0,4]),
                ([1,2,3,4],27,[1,0,2,3,4,0]),([9,1,2,3,4],27,[1,9,2,3,4,0]),
                ([1,2,3,4],30,[0,1,2,3,4,0]),([1,2,3,4,9],30,[0,1,2,3,4,9]),
                ([1,2,3,4],31,[1,0,2,3,0,4]),([1,2,3,4,9],31,[1,0,2,3,9,4])]
        for args,op,expected in curves:same(move+command(args,op)+end,move+command(expected,8)+end)
        same(move+command([1,2,3,4,5,6,7,8],30)+end,move+command([0,1,2,3,4,0,5,0,6,7,0,8],8)+end)
        same(move+command([1,2,3,4,5,6,7,8],24)+end,move+command([1,2,3,4,5,6],8)+command([7,8],5)+end)
        same(move+command([1,2,3,4,5,6,7,8],25)+end,move+command([1,2],5)+command([3,4,5,6,7,8],8)+end)
        flex=[(34,[1,2,3,4,5,6,7],[1,0,2,3,4,0,5,0,6,-3,7,0]),
              (35,list(range(1,13))+[50],list(range(1,13))),
              (36,list(range(1,10)),[1,2,3,4,5,0,6,0,7,8,9,-14]),
              (37,[10,1,10,1,10,1,10,1,10,1,7],[10,1,10,1,10,1,10,1,10,1,7,-5]),
              (37,[1,10,1,10,1,10,1,10,1,10,7],[1,10,1,10,1,10,1,10,1,10,-5,7])]
        for op,args,expected in flex:same(move+command(args,(12,op))+end,move+command(expected,8)+end)
        arithmetic=[([2,3],3,1),([0,3],3,0),([0,3],4,1),([0],5,1),([-7],9,7),
                    ([2,3],10,5),([2,3],11,-1),([7,2],12,3.5),([7],14,-7),([2,2],15,1),
                    ([2,3],24,6),([9],26,3),([10,20,1,2],22,10),([10,20,3,2],22,20)]
        for args,op,value in arithmetic:
            same(command(args,(12,op))+command([0],21)+end,command([value,0],21)+end)
        same(command([10],(12,27))+b'\x15'+end,command([10,10],21)+end)
        same(command([10,20],(12,28))+b'\x15'+end,command([20,10],21)+end)
        same(command([10,0],(12,29))+b'\x15'+end,command([10,10],21)+end)
        same(command([10,-1],(12,29))+b'\x15'+end,command([10,10],21)+end)
        same(command([10,20,2,1],(12,30))+b'\x15'+end,command([20,10],21)+end)
        same(command([10,20,2,-1],(12,30))+b'\x15'+end,command([20,10],21)+end)
        same(command([10,31],(12,20))+command([31],(12,21))+command([0],21)+end,command([10,0],21)+end)
        same(command([99],(12,18))+move+end,move+end)
        same(command([-107],10)+command([10,20],5)+end,move+command([10,20],5)+end,local_subrs=[move+b'\x0b'])
        same(command([-107],29)+b'\x15'+end,command([10,20],21)+end,global_programs=[command([10,20],11)])
        same(command([-107],10),move+end,local_subrs=[move+end])
        same(command([-107],29)+end,move+end,global_programs=[command([-107],10)+b'\x0b'],local_subrs=[move+b'\x0b'])
        for value in (-32768,-1131,-108,-107,0,107,108,1131,32767,.25,-.25):
            result=render(command([value,0],21)+end)
            assert result[1][0][1]==value,(value,result)
        composite=render(command([5,10,65,66],14));assert composite[2]==['1','65','66','5','10']
        random=render(bytes([12,23])+command([0],21)+end);assert 0<random[1][0][1]<=1
        invalid=[b'',b'\x0b',b'\x0f',b'\x1c',b'\xff\0',b'\x0c',b'\xf7',b'\x13',
                 command([0,10],1)+b'\x13',command([10],5)+end,command([10,20],5)+end,
                 command([1,0],(12,12))+end,command([-1],(12,26))+end,
                 command([0],(12,21))+end,command([1,32],(12,20))+end,
                 command([1,2],(12,29))+end,command([1,2,3,1],(12,30))+end,
                 command([1]*49,21)+end,move+command([1,2,3],8)+end,
                 command([-108],10)+end,command([0],10)+end,command([1,2,3,256],14),
                 command([0,0],21)+command([700],14),command([0]*48,1)*5+end,
                 command([0],(12,18))*40000+end]
        for code in invalid:render(code,False)
        render(command([-107],10)+end,False,local_subrs=[command([-107],10)+b'\x0b'])
    print(f'Type 2 charstrings: {checks} checks passed')


if __name__=='__main__':main()
