#!/usr/bin/env python3
"""Check isolated-mask composition, retained work limits and clipped functions."""
import itertools
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from generate_pdf_group_work_fixtures import mask_page,clipped_color_page
from test_pdf_pages import document
from test_pdf_forms import form


def main():
    probe,allocator=[str(Path(x).resolve()) for x in sys.argv[1:3]]
    checks=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupid-group-work-') as directory:
        path=Path(directory)/'case.pdf'
        def render(data,reject=False,aa=False):
            nonlocal checks
            path.write_bytes(data);r=subprocess.run([probe,str(path),'0',*(['--pdf-text-aa','--pdf-graphics-aa'] if aa else [])],capture_output=True,timeout=60)
            assert (r.returncode!=0)==reject,(r.returncode,r.stderr)
            checks+=1
            if reject:return None
            w,h,p=struct.unpack('<III',r.stdout[:12]);assert p==1 and len(r.stdout)==12+w*h*4
            return w,h,r.stdout[12:]
        def fault(data,reject=False):
            nonlocal allocations
            path.write_bytes(data);r=subprocess.run([allocator,str(path),str(int(reject))],capture_output=True,text=True,check=True,timeout=120)
            allocations+=int(r.stdout.split('checks')[0].split()[-1])
        for space in ('/DeviceGray','/DeviceCMYK'):
            w,h,pixels=render(mask_page(space=space))
            assert (w,h)==(816,1056) and pixels==bytes((255,0,0,255))*(w*h)
            small=mask_page(size=(18,12),space=space)
            assert render(small)[2]==bytes((255,0,0,255))*(24*16)
            fault(small)
        # Keeping the same memory limit still rejects a deeper CMYK stack.
        render(mask_page(depth=6,space='/DeviceCMYK'),True)
        for image,aa in itertools.product((False,True),(False,True)):
            data=clipped_color_page(image=image)
            w,h,pixels=render(data,aa=aa)
            assert (w,h)==(480,320)
            expected=bytes(c for y in range(h) for x in range(w) for c in
                ((0,0,0,255) if 4<=x<16 and 308<=y<316 else (255,255,255,255)))
            assert pixels==expected,(image,aa)
            assert render(clipped_color_page(repeats=1,image=image),aa=aa)[2]==pixels
        # The optimization does not increase the color-evaluation budget.
        render(clipped_color_page(clip=False),True)
        for kind,knockout,backdrop,alpha in itertools.product(('Alpha','Luminosity'),(False,True),(0,1),(0,.5,1)):
            program='1 0 0 rg 0 0 18 12 re f /A gs 0 0 1 rg 0 0 18 12 re f'
            mask=form(program,'/BBox [0 0 18 12] /Group << /S /Transparency /CS /DeviceRGB /I true /K '+str(knockout).lower()+' >> /Resources << /ExtGState << /A << /ca '+str(alpha)+' >> >> >>')
            state='<< /SMask << /S /'+kind+' /G 10 0 R /BC ['+' '.join([str(backdrop)]*3)+'] >> >>'
            data=document('/MediaBox [0 0 18 12] /Resources << /ExtGState << /M 20 0 R >> >>',
                          stream='/M gs 0 g 0 0 18 12 re f',extra={10:mask,20:state})
            value=(alpha if knockout else 1) if kind=='Alpha' else (
                (1-alpha)*backdrop+alpha*.11 if knockout else (1-alpha)*.3+alpha*.11)
            expected=int((1-value)*255+.5)
            assert max(abs(v-expected) for i,v in enumerate(render(data)[2]) if i%4!=3)<=1
            if kind=='Luminosity' and alpha==.5:fault(data)
        for bad in ('/I 1','/I true /K /Invalid'):
            mask=form('1 g 0 0 18 12 re f','/BBox [0 0 18 12] /Group << /S /Transparency /CS /DeviceGray '+bad+' >>')
            data=document('/MediaBox [0 0 18 12] /Resources << /ExtGState << /M << /SMask << /S /Alpha /G 10 0 R >> >> >> >>',
                          stream='/M gs 0 g 0 0 18 12 re f',extra={10:mask})
            render(data,True);fault(data,True)
    print(f'PDF group work: {checks} page checks; {allocations} allocation checks')


if __name__=='__main__':main()
