#!/usr/bin/env python3
"""Check Type 1 widths, paths, standard OtherSubrs and malformed bytecode."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_type1 import fixture


def command(values,op):
    data=bytearray()
    for n in values:
        assert int(n)==n;n=int(n)
        if -107<=n<=107:data.append(n+139)
        elif 108<=n<=1131:data.extend((247+(n-108)//256,(n-108)%256))
        elif -1131<=n<=-108:data.extend((251+(-n-108)//256,(-n-108)%256))
        else:data.extend(b'\xff'+struct.pack('>i',n))
    data.extend([op] if isinstance(op,int) else op)
    return bytes(data)


WIDTH=command([0,600],13)
BOX=WIDTH+command([0,0],21)+command([400,0],5)+command([0,600],5)+command([-400,0],5)+b'\x09\x0e'


def main():
    probe=str(Path(sys.argv[1]).resolve());checks=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-type1-charstrings-') as tmp:
        path=Path(tmp)/'font.pfa'
        def render(code,ok=True,**kwargs):
            nonlocal checks
            data,l1,l2=fixture(glyphs=[('.notdef',WIDTH+b'\x0e'),('A',code)],**kwargs);path.write_bytes(data)
            result=subprocess.run([probe,str(path),str(l1),str(l2),'--outline'],capture_output=True,text=True,timeout=10)
            assert result.returncode==(0 if ok else 3),(code[:40],result.returncode,result.stderr,result.stdout)
            checks+=1
            if not ok:return None
            lines=result.stdout.splitlines();i=next(i for i,line in enumerate(lines) if line.startswith('outline 1 '))
            return lines[i:]
        def same(a,b,**kwargs):
            left=render(a,**kwargs);right=render(b,**kwargs);assert left==right,(left,right)
        move=command([0,0],21);end=b'\x0e'
        for iv in (-1,0,4):assert render(BOX,len_iv=iv)[0]=='outline 1 600 0 5'
        same(WIDTH+move+command([400],6)+command([600],7)+command([-400],6)+b'\x09'+end,BOX)
        same(command([20,600],13)+move+end,command([20,0,600,0],(12,7))+move+end)
        same(WIDTH+command([20],22)+command([30],4)+end,WIDTH+command([20,0],21)+command([0,30],21)+end)
        same(WIDTH+move+command([1,2,3,4],30)+end,WIDTH+move+command([0,1,2,3,4,0],8)+end)
        same(WIDTH+move+command([1,2,3,4],31)+end,WIDTH+move+command([1,0,2,3,0,4],8)+end)
        same(WIDTH+command([7,2],(12,12))+command([0],21)+end,WIDTH+command([7,2],(12,12))+command([0],21)+end)
        assert 'segment 0 3.5 0 0 0 0 0' in render(WIDTH+command([7,2],(12,12))+command([0],21)+end)
        for op in (1,3):same(WIDTH+command([0,20],op)+move+end,WIDTH+move+end)
        for op in (1,2):same(WIDTH+command([0,20,30,20,60,20],(12,op))+move+end,WIDTH+move+end)
        same(WIDTH+bytes([12,0])+move+end,WIDTH+move+end)
        same(WIDTH+command([0],10)+end,WIDTH+move+end,subrs=[move+b'\x0b'])
        same(WIDTH+command([0],10)+b'\x15'+end,WIDTH+command([20,30],21)+end,subrs=[command([20,30],11)])
        same(WIDTH+command([0,1,3],(12,16))+bytes([12,17,10])+end,WIDTH+move+end,subrs=[move+b'\x0b'])
        subrs=[command([3,0],(12,16))+bytes([12,17,12,17,12,33,11]),
               command([0,1],(12,16))+b'\x0b',command([0,2],(12,16))+b'\x0b',b'\x0b']
        flex=WIDTH+move+command([1],10)
        for x,y in [(50,0),(-35,0),(10,10),(25,0),(25,0),(10,-10),(15,0)]:flex+=command([x,y],21)+command([2],10)
        flex+=command([50,100,0,0],10)+end
        expected=WIDTH+move+command([15,0,10,10,25,0],8)+command([25,0,10,-10,15,0],8)+end
        same(flex,expected,subrs=subrs)
        for value in (-2147483648,-1132,-1131,-108,-107,0,107,108,1131,1132,2147483647):
            result=render(WIDTH+command([value,0],21)+end)
            assert float(result[1].split()[2])==float(f'{value:.9g}'),(value,result)
        # A closepath does not reset the Type 1 current point.
        result=render(WIDTH+move+command([10,20],5)+b'\x09'+command([5,5],21)+end)
        assert 'segment 0 15 25 0 0 0 0' in result
        invalid=[b'',end,WIDTH+WIDTH+end,WIDTH+b'\x0b',WIDTH+b'\xff\0',WIDTH+b'\x1c',
                 WIDTH+command([1],5)+end,WIDTH+command([1,2],6)+end,WIDTH+command([1,2],5)+end,
                 WIDTH+command([1,0],(12,12))+end,WIDTH+bytes([12,17])+end,
                 WIDTH+command([0,4],(12,16))+end,WIDTH+command([1],10)+end,
                 WIDTH+command([1]*25,21)+end,WIDTH+command([0,1],(12,16))+end,
                 WIDTH+command([0,2],(12,16))+end,WIDTH+command([0,0],(12,16))+end,
                 WIDTH+command([0,0,0,65,256],(12,6))]
        for code in invalid:render(code,False)
        render(WIDTH+command([0],10)+end,False,subrs=[command([0],10)+b'\x0b'])
        render(WIDTH+command([0],10)*4+end,False,subrs=[bytes([12,0])*20000+b'\x0b'])
    print(f'Type 1 charstrings: {checks} checks passed')


if __name__=='__main__':main()
