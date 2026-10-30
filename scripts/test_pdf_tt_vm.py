#!/usr/bin/env python3
"""Execute the font-program initialization stage of the pinned TrueType faces."""
from pathlib import Path
import subprocess
import sys
from generate_pdf_extended_fonts import FONTS, SOURCE
from test_pdf_truetype import tables


def main():
    probe=str(Path(sys.argv[1]).resolve())
    checked=0
    prepared=0
    for filename in FONTS.values():
        result=subprocess.run([probe,'--font',str(SOURCE/filename)],capture_output=True,text=True,timeout=20)
        assert result.returncode==0,(filename,result.stdout,result.stderr)
        ok,functions,error,offset,opcode,cvt,fpgm,prep=map(int,result.stdout.split())
        assert (ok,functions,error,offset,opcode)==(1,71,0,0,0),(filename,result.stdout)
        directory=tables((SOURCE/filename).read_bytes())
        assert (cvt,fpgm,prep)==tuple(directory[tag][2] for tag in (b'cvt ',b'fpgm',b'prep'))
        checked+=1
        for mode in ('--prep','--prep-gray'):
            for ppem in range(1,97):
                result=subprocess.run([probe,mode,str(SOURCE/filename),str(ppem)],capture_output=True,text=True,timeout=20)
                assert result.returncode==0,(filename,mode,ppem,result.stdout,result.stderr)
                assert tuple(map(int,result.stdout.split()[:5]))==(1,71,0,0,0),(filename,mode,ppem,result.stdout)
                prepared+=1
    print(f'TrueType font-program initialization: {checked} pinned faces; 71 definitions each; {prepared} uniform-size preparation runs; glyph hinting not enabled')


if __name__=='__main__':main()
