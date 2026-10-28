#!/usr/bin/env python3
"""Check fallback font advances, cached widths and allocation-error propagation."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_cff_render import fixture as cff_pdf, BOX as CFF_BOX
from test_pdf_cff import fixture as cff_font
from test_pdf_type2 import number, command as cff_command
from test_pdf_type1_render import fixture as type1_pdf
from test_pdf_type1_charstrings import command, BOX as TYPE1_BOX, WIDTH


def main():
    probe,allocator=[str(Path(arg).resolve()) for arg in sys.argv[1:3]]
    checks=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-font-advances-') as directory:
        path=Path(directory)/'case.pdf'
        def render(pdf,reject=False):
            nonlocal checks
            path.write_bytes(pdf)
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert run.returncode==(3 if reject else 0),run.stderr
            checks+=1
            return run.stdout
        def fault(pdf,reject=False):
            nonlocal allocations
            path.write_bytes(pdf)
            run=subprocess.run([allocator,str(path),'1' if reject else '0'],capture_output=True,text=True,timeout=40)
            assert run.returncode==0,run.stdout+run.stderr
            allocations+=int(run.stdout.split(': ')[1].split(' checks')[0])
        for kind in ('cff','type1'):
            for width in (-200,0,600,1000):
                if kind=='cff':
                    font=cff_font(programs=[b'\x0e',b'\x0e',number(width-100)+CFF_BOX,b'\x0e'])
                    make=lambda content,metrics: cff_pdf(content,font=font,metrics=metrics)
                else:
                    glyphs=[('.notdef',WIDTH+b'\x0e'),('A',command([0,width],13)+TYPE1_BOX[len(WIDTH):])]
                    make=lambda content,metrics: type1_pdf(content,glyphs=glyphs,metrics=metrics)
                explicit=f'/FirstChar 65 /LastChar 65 /Widths [{width}]'
                for mode in range(8):
                    content=f'BT /F 18 Tf 48 36 Td {mode} Tr (AAA) Tj 0 Tr (A) Tj ET'
                    if mode>=4:content+=' 1 0 0 rg 0 0 120 90 re f'
                    assert render(make(content,''))==render(make(content,explicit)),(kind,width,mode)
                content='BT /F 30 Tf 20 30 Td (AAA) Tj ET'
                fault(make(content,''))
            malformed=b'\x02'
            if kind=='cff':
                bad=lambda content: cff_pdf(content,font=cff_font(programs=[b'\x0e',b'\x0e',malformed,b'\x0e']),metrics='')
            else:
                bad=lambda content: type1_pdf(content,glyphs=[('.notdef',WIDTH+b'\x0e'),('A',malformed)],metrics='')
            pdf=bad('BT /F 30 Tf 3 Tr (A) Tj ET')
            render(pdf,True);fault(pdf,True)
        # The Type 1 width is a vector transformed by FontMatrix, not just wx.
        glyphs=[('.notdef',WIDTH+b'\x0e'),('A',command([0,0,600,200],[12,7])+TYPE1_BOX[len(WIDTH):])]
        content='BT /F 30 Tf 20 30 Td (AAA) Tj ET'
        kwargs={'glyphs':glyphs,'matrix':'[.001 0 .0005 .001 0 0]'}
        assert render(type1_pdf(content,metrics='',**kwargs))==render(type1_pdf(content,metrics='/FirstChar 65 /LastChar 65 /Widths [700]',**kwargs))
        fault(type1_pdf(content,metrics='',**kwargs))
        accent=cff_command([0,0],21)+cff_command([100,0,0,100,-100,0],5)+b'\x0e'
        composite=cff_command([900,100,600,65,194],14)
        font=cff_font(glyphs=5,charset=b'\0\0\1\0\x22\0\x7d\0\xab',strings=(),programs=[b'\x0e',b'\x0e',CFF_BOX,accent,composite])
        content='BT /F 30 Tf 20 30 Td <C1C1C1> Tj ET'
        assert render(cff_pdf(content,font=font,metrics=''))==render(cff_pdf(content,font=font,metrics='/FirstChar 193 /LastChar 193 /Widths [1000]'))
        fault(cff_pdf(content,font=font,metrics=''))
        composite=command([0,1000],13)+command([0,100,600,65,194],(12,6))
        accent=WIDTH+command([0,0],21)+command([100,0],5)+command([0,100],5)+command([-100,0],5)+b'\x09\x0e'
        glyphs=[('.notdef',WIDTH+b'\x0e'),('A',TYPE1_BOX),('acute',accent),('Aacute',composite)]
        assert render(type1_pdf(content,glyphs=glyphs,metrics=''))==render(type1_pdf(content,glyphs=glyphs,metrics='/FirstChar 193 /LastChar 193 /Widths [1000]'))
        fault(type1_pdf(content,glyphs=glyphs,metrics=''))
        for count,reject in ((65536,False),(65537,True)):
            render(cff_pdf('BT /F 1 Tf 3 Tr ('+'A'*count+') Tj ET',metrics=''),reject)
    print(f'PDF font advance checks: {checks}; {allocations} allocation/cache comparisons')


if __name__=='__main__':
    main()
