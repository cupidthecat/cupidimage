#!/usr/bin/env python3
"""Optional independent Type 2 outline comparison; requires FontTools."""
import io
import math
from pathlib import Path
import subprocess
import sys
from fontTools.cffLib import CFFFontSet
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import RecordingPen
from fontTools.encodings.StandardEncoding import StandardEncoding
from generate_pdf_standard_outlines import FONTS, font_bytes

probe=str(Path(sys.argv[1]).resolve())
count=0
for name,stem in sorted(FONTS.items()):
    data=font_bytes(stem)
    cff=CFFFontSet();cff.decompile(io.BytesIO(data),TTFont())
    font=cff[cff.fontNames[0]]
    result=subprocess.run([probe,name,'--dump'],capture_output=True,text=True,timeout=20)
    assert result.returncode==0,(name,result.stderr)
    lines=iter(result.stdout.splitlines())
    for gid,glyph_name in enumerate(font.charset):
        fields=next(lines).split();assert fields[0]=='glyph' and int(fields[1])==gid
        width=float(fields[2]);segments=int(fields[3]);composite=int(fields[4])
        actual=[]
        for _ in range(segments):
            row=next(lines).split();assert row[0]=='segment'
            kind=int(row[1]);points=list(map(float,row[2:]))
            actual.append((('moveTo','lineTo','curveTo','closePath')[kind],tuple(tuple(points[i:i+2]) for i in range(0,len(points),2))))
        if composite:
            actual.extend([('addComponent',(StandardEncoding[int(fields[5])],(1,0,0,1,0,0))),
                           ('addComponent',(StandardEncoding[int(fields[6])],(1,0,0,1,float(fields[7]),float(fields[8]))))])
        pen=RecordingPen();charstring=font.CharStrings[glyph_name];charstring.draw(pen)
        expected=pen.value
        assert math.isclose(width,charstring.width,abs_tol=1e-8),(name,glyph_name,'width',width,charstring.width)
        assert len(actual)==len(expected),(name,glyph_name,'segment count',len(actual),len(expected))
        def equal(a,b):
            if isinstance(a,(int,float)) and isinstance(b,(int,float)):return math.isclose(a,b,rel_tol=1e-12,abs_tol=1e-8)
            if isinstance(a,tuple) and isinstance(b,tuple):return len(a)==len(b) and all(equal(x,y) for x,y in zip(a,b))
            return a==b
        assert all(equal(a,b) for a,b in zip(actual,expected)),(name,glyph_name,'outline differs')
        count+=1
    assert next(lines,None) is None
print(f'Independent standard-font Type 2 comparison: {count} glyph outlines and advances match')
