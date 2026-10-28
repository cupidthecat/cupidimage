#!/usr/bin/env python3
"""Verify horizontal glyph origins independently of PDF advance widths."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_embedded_fonts import FONT, fixture
from test_pdf_truetype import tables, replace_tables
from audit_pdf_corpus import read_ppm


def bearing_font(bearings, short=False, use_metrics=None, box_x=0):
    mapping=tables(FONT)
    def table(tag):
        _,at,length=mapping[tag]
        return bytearray(FONT[at:at+length])
    hhea=table(b'hhea');count=struct.unpack_from('>H',table(b'maxp'),4)[0]
    metrics=[bearings.get(glyph,0) for glyph in range(count)]
    if short:
        struct.pack_into('>H',hhea,34,1)
        hmtx=struct.pack('>H'+'h'*count,1000,*metrics)
    else:
        struct.pack_into('>H',hhea,34,count)
        hmtx=b''.join(struct.pack('>Hh',1000,value) for value in metrics)
    head=table(b'head')
    struct.pack_into('>H',head,16,struct.unpack_from('>H',head,16)[0]&~2)
    changes={b'hhea':hhea,b'hmtx':hmtx,b'head':head}
    if box_x:
        original=table(b'glyf');loca=table(b'loca')
        offsets=[n*2 for n in struct.unpack('>'+str(count+1)+'H',loca)]
        glyphs=[original[offsets[i]:offsets[i+1]] for i in range(count)]
        glyphs[1]=struct.pack('>hhhhhHH',1,box_x,0,box_x+400,600,3,0)+bytes([1]*4)+struct.pack('>8h',box_x,0,400,0,0,600,0,-600)
        offsets=[0]
        for glyph in glyphs:offsets.append(offsets[-1]+len(glyph))
        struct.pack_into('>H',head,50,1)
        changes.update({b'glyf':b''.join(glyphs),b'loca':struct.pack('>'+str(count+1)+'I',*offsets)})
    if use_metrics is not None:
        glyf=table(b'glyf');loca=table(b'loca')
        start=struct.unpack_from('>H',loca,4*2)[0]*2
        at=start+10
        if use_metrics==1:
            flags=struct.unpack_from('>H',glyf,at)[0]
            at+=4+(4 if flags&1 else 2)+(2 if flags&8 else 4 if flags&64 else 8 if flags&128 else 0)
        flags=struct.unpack_from('>H',glyf,at)[0]
        struct.pack_into('>H',glyf,at,flags|512)
        changes[b'glyf']=glyf
    return replace_tables(FONT,changes)


def main():
    probe,outline,allocator=[str(Path(arg).resolve()) for arg in sys.argv[1:4]]
    checks=references=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-bearings-') as directory:
        root=Path(directory);path=root/'case.pdf';font_path=root/'font.ttf'
        def render(pdf):
            path.write_bytes(pdf)
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert run.returncode==0,run.stderr
            return run.stdout
        def same(content,expected,font,reference=False,**kwargs):
            nonlocal checks,references
            pdf=fixture(content,font_data=font,**kwargs)
            actual=render(pdf)
            assert actual==render(fixture(expected)),content
            checks+=1
            if reference and '--reference' in sys.argv:
                path.write_bytes(pdf);prefix=root/'reference'
                run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                assert run.returncode==0,run.stderr
                w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                assert (w,h)==(160,120) and rgb==bytes(v for i,v in enumerate(actual[12:]) if i%4!=3),content
                references+=1
        def reference_pair(actual,expected):
            nonlocal references
            if '--reference' not in sys.argv:return
            images=[]
            for index,pdf in enumerate((actual,expected)):
                path.write_bytes(pdf);prefix=root/str(index)
                run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                assert run.returncode==0,run.stderr
                images.append(read_ppm(prefix.with_suffix('.ppm')))
            assert images[0]==images[1],'component metrics differ in independent raster'
            references+=1
        def points(font,glyph):
            font_path.write_bytes(font)
            run=subprocess.run([outline,str(font_path),str(glyph)],capture_output=True,text=True,timeout=10)
            assert run.returncode==0,run.stderr
            return [list(map(float,line.split())) for line in run.stdout.splitlines()[1:]]
        base={glyph:points(FONT,glyph) for glyph in (1,4,5)}
        for short in (False,True):
            for bearing in (-200,100,200):
                font=bearing_font({1:bearing,4:bearing,5:bearing},short)
                for glyph in base:
                    actual=points(font,glyph)
                    expected=[[x+bearing,y,on,end] for x,y,on,end in base[glyph]]
                    assert actual==expected,(short,bearing,glyph)
                    checks+=1
                shift=bearing*.03
                box=f'{20+shift} 30 12 18 re f'
                for cid in (False,True):
                    text='<0001>' if cid else '(A)'
                    same(f'BT /F 30 Tf 20 30 Td {text} Tj ET',box,font,True,cid=cid)
                same('BT /F 30 Tf 20 30 Td (AA) Tj ET',box+f' {50+shift} 30 12 18 re f',font,True)
                same('BT /F 30 Tf 20 30 Td (AA) Tj ET',box+f' {35+shift} 30 12 18 re f',font,True,
                     metrics='/FirstChar 65 /LastChar 65 /Widths [500]')
                same('BT /F 30 Tf 7 Tr 20 30 Td (A) Tj ET 1 0 0 rg 0 0 120 90 re f',
                     f'{20+shift} 30 12 18 re W n 1 0 0 rg 0 0 120 90 re f',font)
                same('1 w BT /F 30 Tf 1 Tr 20 30 Td (A) Tj ET',f'1 w {20+shift} 30 12 18 re S',font)
                same('2 0 0 1 0 0 cm BT /F 30 Tf 20 30 Td (A) Tj ET',
                     f'2 0 0 1 0 0 cm {20+shift} 30 12 18 re f',font)
        for xmin in (-80,80):
            for bearing in (0,100):
                font=bearing_font({1:bearing},box_x=xmin)
                assert points(font,1)==[[x+bearing,y,on,end] for x,y,on,end in base[1]]
                checks+=1
                same('BT /F 30 Tf 20 30 Td (A) Tj ET',f'{20+bearing*.03} 30 12 18 re f',font,True)
        # Components retain their design-space positions before the final origin shift.
        for component,shift in ((0,100),(1,100)):
            font=bearing_font({1:100,4:-500},use_metrics=component)
            assert points(font,4)==[[x+shift,y,on,end] for x,y,on,end in base[4]]
            checks+=1
            dx=shift*.03
            same('BT /F 30 Tf 20 30 Td (D) Tj ET',
                 f'{20+dx} 30 12 18 re f {35+dx} 30 m {39.5+dx} 43.5 l {45.5+dx} 43.5 l {41+dx} 30 l h f',font)
            reference_pair(fixture('BT /F 30 Tf 20 30 Td (D) Tj ET',font_data=font),
                           fixture(f'1 0 0 1 {dx} 0 cm BT /F 30 Tf 20 30 Td (D) Tj ET'))
        for nested_metrics,shift in ((False,200),(True,100)):
            font=bearing_font({1:100,4:-500,5:200},use_metrics=0)
            mapping=tables(font);_,ga,gl=mapping[b'glyf'];_,la,ll=mapping[b'loca']
            glyf=bytearray(font[ga:ga+gl]);loca=font[la:la+ll]
            d=struct.unpack_from('>H',loca,8)[0]*2
            e=struct.unpack_from('>H',loca,10)[0]*2
            struct.pack_into('>H',glyf,d+12,5)
            if nested_metrics:
                struct.pack_into('>H',glyf,e+10,struct.unpack_from('>H',glyf,e+10)[0]|512)
            font=replace_tables(font,{b'glyf':glyf})
            assert points(font,4)==[[x+shift,y,on,end] for x,y,on,end in base[5]+base[4][4:]]
            checks+=1
        for font in (bearing_font({1:200},True),bearing_font({1:100,4:-500},use_metrics=0)):
            path.write_bytes(fixture('BT /F 30 Tf 20 30 Td (ADADA) Tj ET',font_data=font))
            run=subprocess.run([allocator,str(path),'0'],capture_output=True,text=True,timeout=30)
            assert run.returncode==0,run.stdout+run.stderr
            allocations+=int(run.stdout.split(': ')[1].split(' checks')[0])
    print(f'TrueType bearing checks: {checks}; {references} independent comparisons; {allocations} allocation checks')


if __name__=='__main__':
    main()
