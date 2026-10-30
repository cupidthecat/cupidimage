#!/usr/bin/env python3
"""Check explicitly enabled native TrueType page hinting without changing PDF advances."""
import base64
import ctypes
import itertools
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_embedded_fonts import fixture as base_fixture

FIXTURE=json.loads(Path('tests/fixtures/pdf-page-hinting.json').read_text())
FONT=base64.b64decode(FIXTURE['font'])
POINT_SIZE_FONT=base64.b64decode(FIXTURE['point_size_font'])
RATIO=struct.unpack('f',struct.pack('f',96/72))[0]


def font_descriptor(data, name='CupidImageHintedShapes', file_object=9):
    from test_pdf_truetype import tables
    mapping=tables(data)
    head=mapping[b'head'][1];hhea=mapping[b'hhea'][1];post=mapping[b'post'][1]
    units=struct.unpack_from('>H',data,head+18)[0]
    box=struct.unpack_from('>4h',data,head+36)
    ascent,descent=struct.unpack_from('>2h',data,hhea+4)
    angle=struct.unpack_from('>i',data,post+4)[0]/65536
    fixed=struct.unpack_from('>I',data,post+12)[0]
    flags=32+(64 if angle else 0)+(1 if fixed else 0)+(2 if 'Serif' in name else 0)
    os2=mapping[b'OS/2'];cap=835
    if os2[2]>=90 and struct.unpack_from('>H',data,os2[1])[0]>=2:
        cap=struct.unpack_from('>h',data,os2[1]+88)[0] or cap
    bbox=' '.join(f'{v*1000/units:.17g}' for v in box)
    return (f'<< /Type /FontDescriptor /FontName /{name} /Flags {flags} /FontBBox [{bbox}] '
            f'/Ascent {ascent*1000/units:.17g} /Descent {descent*1000/units:.17g} '
            f'/CapHeight {cap*1000/units:.17g} /ItalicAngle {angle} /StemV 80 '
            f'/FontFile2 {file_object} 0 R >>').encode()


def fixture(content, font_data=FONT, font_name='CupidImageHintedShapes', **kwargs):
    objects=dict(kwargs.pop('extra',None) or {})
    objects.setdefault(8,font_descriptor(font_data,font_name))
    return base_fixture(content,font_data=font_data,font_name=font_name,extra=objects,**kwargs)


def reference_image(face, char, size, tm, tx, ty, freetype, outline_state, reference_coverage):
    a,b,c,d=tm
    units=face.units_per_EM
    scale=size/units
    m=(a*scale*RATIO,-b*scale*RATIO,c*scale*RATIO,-d*scale*RATIO,
       (tx+1.125)*RATIO,(90-ty)*RATIO)
    ppem=max(1,math.floor(math.hypot(m[2],m[3])*units+.5))
    fixed=[math.trunc(v*units/ppem*65536) for v in m[:4]]
    ox,oy=[math.floor(v*64+.5) for v in m[4:]]
    face.set_pixel_sizes(ppem,ppem)
    face.set_transform(freetype.Matrix(fixed[0],fixed[2],-fixed[1],-fixed[3]),
                       freetype.Vector(ox,120*64-oy))
    face.load_char(char,freetype.FT_LOAD_NO_BITMAP|freetype.FT_LOAD_NO_AUTOHINT)
    mask=reference_coverage(outline_state(face),160,120)
    expected=bytearray(b'\xff'*(160*120*4))
    for y in range(120):
        for x in range(160):
            value=255-mask[(119-y)*160+x]
            at=(y*160+x)*4;expected[at+1]=expected[at+2]=value
    return expected


def main():
    probe,allocator=map(lambda p:str(Path(p).resolve()),sys.argv[1:3])
    checks=allocations=references=0
    with tempfile.TemporaryDirectory(prefix='cupid-page-hint-') as directory:
        path=Path(directory)/'case.pdf';font_path=Path(directory)/'font.ttf';font_path.write_bytes(FONT)
        def render(content,aa=1,hint=1,**kwargs):
            nonlocal checks
            path.write_bytes(fixture(content,font_data=kwargs.pop("font_data",FONT),**kwargs))
            result=subprocess.run([probe,str(path),'0',str(aa),str(hint)],capture_output=True,timeout=20)
            assert result.returncode==0,(result.returncode,result.stderr,content)
            checks+=1
            return result.stdout[12:]
        for aa,char,size in itertools.product((0,1),('A','B'),(12,18,30)):
            content=f'1 0 0 rg BT /F {size} Tf 20.1875 30.1875 Td ({char}) Tj ET'
            assert render(content,aa)!=render(content,aa,0),(aa,char,size,'instructions not applied')
            automatic=f'BT /F {size} Tf 20.1875 30.1875 Td ({char}{char}) Tj ET'
            explicit=f'BT /F {size} Tf 20.1875 30.1875 Td ({char}) Tj 1 0 0 1 {20.1875+size} 30.1875 Tm ({char}) Tj ET'
            assert render(automatic,aa)==render(explicit,aa),'hinted phantom changed PDF advance'
            path.write_bytes(fixture(automatic,font_data=FONT))
            result=subprocess.run([allocator,str(path),'0',str(aa),'1'],capture_output=True,text=True,timeout=120)
            assert result.returncode==0,(result.stdout,result.stderr)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        # Equal rounded ppem does not make distinct physical point sizes interchangeable.
        a='1 0 0 rg BT /F 18.1 Tf 20.125 30 Td (A) Tj ET'
        b='1 0 0 rg BT /F 18.2 Tf 70.125 30 Td (A) Tj ET'
        first=render(a,font_data=POINT_SIZE_FONT)
        second=render(b,font_data=POINT_SIZE_FONT)
        assert first==render(a,font_data=base64.b64decode(FIXTURE['point_size_1158_font'])),'physical MPS value at 18.1 points'
        assert second==render(b,font_data=base64.b64decode(FIXTURE['point_size_1165_font'])),'physical MPS value at 18.2 points'
        merged=bytes(min(x,y) for x,y in zip(first,second))
        assert render(a+' '+b,font_data=POINT_SIZE_FONT)==merged,'MPS cache key lost point size'
        assert render(b+' '+a,font_data=POINT_SIZE_FONT)==merged,'MPS cache key depends on order'
        # CID placement and vertical advances remain PDF metrics, not phantom advances.
        for aa,vertical in itertools.product((0,1),(False,True)):
            metrics='/DW 1000 /W [1 [400 700]] /W2 [1 [-1100 200 900 -800 350 950]]'
            automatic='BT /F 18 Tf 60 70 Td <00010002> Tj ET'
            tx,ty=(60,50.2) if vertical else (67.2,70)
            explicit=f'BT /F 18 Tf 60 70 Td <0001> Tj 1 0 0 1 {tx} {ty} Tm <0002> Tj ET'
            assert render(automatic,aa,cid=True,vertical=vertical,metrics=metrics)==render(explicit,aa,cid=True,vertical=vertical,metrics=metrics),'CID advances changed'
        # The hinting switch does not alter other outline formats.
        from test_pdf_cff_render import fixture as cff_fixture
        from test_pdf_type1_render import fixture as type1_fixture
        for maker in (cff_fixture,type1_fixture):
            path.write_bytes(maker('BT /F 18 Tf 20 30 Td (A) Tj ET'))
            results=[subprocess.check_output([probe,str(path),'0','1',str(hint)]) for hint in (0,1)]
            assert results[0]==results[1]
            checks+=2
        # A Form-local font can outlive its resource table only through the hint cache.
        from test_pdf_forms import form
        from test_pdf_type3 import stream
        local_font=b'<< /Type /Font /Subtype /TrueType /FontDescriptor 13 0 R /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 66 /Widths [1000 1000] >>'
        descriptor=font_descriptor(POINT_SIZE_FONT,file_object=14)
        page=b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R /H 12 0 R >> /XObject << /G 20 0 R >> >> >>'
        a='BT /F 12 Tf 20 30 Td (A) Tj ET'
        b='BT /F 18 Tf 60 30 Td (A) Tj ET'
        inline_b=b.replace('/F ', '/H ')
        c='BT /F 24 Tf 20 60 Td (A) Tj ET'
        objects={3:page,12:local_font,13:descriptor,14:stream(POINT_SIZE_FONT,'/Length1 '+str(len(POINT_SIZE_FONT))),
                 20:form(b,'/BBox [0 0 120 90] /Resources << /Font << /F 12 0 R >> >>')}
        for aa in (0,1):
            for actual,expected in (('/G Do',inline_b),(a+' /G Do',a+' '+inline_b),(a+' /G Do '+c,a+' '+inline_b+' '+c)):
                assert render(actual,aa,extra=objects)==render(expected,aa,extra=objects),'Form font cache lifetime/size switch'
                path.write_bytes(fixture(actual,font_data=FONT,extra=objects))
                result=subprocess.run([allocator,str(path),'0',str(aa),'1'],capture_output=True,text=True,timeout=120)
                assert result.returncode==0,(result.stdout,result.stderr)
                allocations+=int(result.stdout.split('checks')[0].split()[-1])
        # Unsupported instructions and a nonterminating glyph fail transactionally.
        from test_pdf_truetype import tables
        mapping=tables(FONT)
        loca=mapping[b'loca'][1]; glyf=mapping[b'glyf'][1]
        head=mapping[b'head'][1]
        long_loca=struct.unpack_from('>h',FONT,head+50)[0]
        start=struct.unpack_from('>I' if long_loca else '>H',FONT,loca+(4 if long_loca else 2))[0]
        if not long_loca:start*=2
        contours=struct.unpack_from('>h',FONT,glyf+start)[0]
        code_at=glyf+start+10+2*contours+2
        for code in (b'\x28', b'\xb8\xff\xfd\x1c'):
            damaged=bytearray(FONT);damaged[code_at:code_at+len(code)]=code
            data=fixture('BT /F 18 Tf 20 30 Td (A) Tj ET',font_data=bytes(damaged))
            path.write_bytes(data)
            for hint,status in ((0,0),(1,3)):
                result=subprocess.run([probe,str(path),'0','1',str(hint)],capture_output=True,timeout=20)
                assert result.returncode==status,(code,result.returncode,result.stderr)
                assert status==0 or not result.stdout
                checks+=1
            for state in ('/F 0 Tf','/F 18 Tf 0 Tz','/F 18 Tf 3 Tr'):
                path.write_bytes(fixture(f'BT {state} 20 30 Td (A) Tj ET',font_data=bytes(damaged)))
                plain=subprocess.check_output([probe,str(path),'0','1','0'])
                hinted=subprocess.check_output([probe,str(path),'0','1','1'])
                assert plain==hinted,'invisible/collapsed text ran instructions'
                checks+=2
            path.write_bytes(data)
            result=subprocess.run([allocator,str(path),'1','1','1'],capture_output=True,text=True,timeout=120)
            assert result.returncode==0,(result.stdout,result.stderr)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        data=fixture('BT /F 18 Tf 20 30 Td (A) Tj ET')
        path.write_bytes(data)
        for value in (-1,2):
            result=subprocess.run([probe,str(path),'0','1',str(value)],capture_output=True)
            assert result.returncode==3 and not result.stdout
            checks+=1
        if len(sys.argv)>3:
            from test_sixel import decode
            binary=str(Path(sys.argv[3]).resolve())
            content='1 0 0 rg BT /F 18 Tf 20 30 Td (A) Tj ET'
            expected=render(content)
            want=[tuple(expected[i+c]*100//255 for c in range(3)) for i in range(0,len(expected),4)]
            data=path.read_bytes()
            for name in ('case.pdf','case.PdF','extensionless'):
                target=Path(directory)/name;target.write_bytes(data)
                result=subprocess.run([binary,'--sixel','--sixel-dither','none','--pdf-page','1',
                                       '--pdf-text-aa','--pdf-tt-hinting',str(target),'160','120'],
                                      capture_output=True,timeout=20)
                assert result.returncode==0,result.stderr
                transparent,dimensions,pixels=decode(result.stdout)
                assert not transparent and dimensions==(160,120)
                assert pixels==want,'CLI native hinting differs from page API'
                checks+=1
            outputs=[]
            for name in ('pages.pdf','pages.PdF'):
                target=Path(directory)/name
                target.write_bytes(Path('assets/pdf/pdfjs/basicapi.pdf').read_bytes())
                result=subprocess.run([binary,'--sixel',str(target),'32','24'],capture_output=True,timeout=20)
                assert result.returncode==0,result.stderr
                outputs.append((result.stdout,result.stderr))
                checks+=1
            assert outputs[0]==outputs[1] and b'[pdf page 2/' in outputs[0][1],'mixed-case PDF lost pages'
        try:
            import freetype
            from pdf_tt_reference import outline_state
            from test_pdf_coverage_reference import reference_coverage
        except (ImportError,SystemExit):
            freetype=None
        if freetype:
            version=ctypes.c_uint(35)
            assert freetype.FT_Property_Set(freetype.get_handle(),b'truetype',b'interpreter-version',ctypes.byref(version))==0
            matrices=((1,0,0,1),(1.5,0,0,.75),(0,1,-1,0),(-1,0,0,1),(.75,.25,-.125,1))
            for char,size,tm,(tx,ty) in itertools.product(('A','B'),(12,18,30),matrices,((20.1875,30.1875),(60,40))):
                face=freetype.Face(str(font_path))
                expected=reference_image(face,char,size,tm,tx,ty,freetype,outline_state,reference_coverage)
                content=f'1 0 0 rg BT /F {size} Tf '+ ' '.join(map(str,(*tm,tx,ty)))+f' Tm ({char}) Tj ET'
                actual=render(content)
                assert actual==expected,(char,size,tm,tx,ty,sum(a!=b for a,b in zip(actual,expected)))
                references+=1
            # The same device outline reaches stroke and deferred text clipping.
            for char in ('A','B'):
                face=freetype.Face(str(font_path))
                reference_image(face,char,18,(1,0,0,1),20.1875,30.1875,
                                freetype,outline_state,reference_coverage)
                path_commands=[]
                first=True
                for x,y,on,end in outline_state(face):
                    assert on
                    px=x/64/RATIO-1.125
                    py=90-(120-y/64)/RATIO
                    path_commands.append(f'{px:.12g} {py:.12g} '+('m' if first else 'l'))
                    first=False
                    if end:
                        path_commands.append('h');first=True
                geometry=' '.join(path_commands)
                glyph=f'BT /F 18 Tf MODE Tr 20.1875 30.1875 Td ({char}) Tj ET'
                assert render('1 w '+glyph.replace('MODE','1'))==render('1 w '+geometry+' S'),'hinted stroke outline differs'
                fill='1 0 0 rg 0 0 120 90 re f'
                assert render(glyph.replace('MODE','7')+' '+fill)==render(geometry+' W n '+fill),'hinted clip outline differs'
            if freetype.version() >= (2,13,3):
                for font in sorted(Path('third_party/extended-standard-fonts').glob('*.ttf')):
                    for (code,char),size,tm in itertools.product(((65,'A'),(103,'g'),(128,'€')),
                                                                (12,18),((1,0,0,1),(0,1,-1,0))):
                        tx,ty=40,30
                        face=freetype.Face(str(font))
                        expected=reference_image(face,char,size,tm,tx,ty,freetype,outline_state,reference_coverage)
                        content=f'1 0 0 rg BT /F {size} Tf '+ ' '.join(map(str,(*tm,tx,ty)))+f' Tm <{code:02x}> Tj ET'
                        actual=render(content,font_data=font.read_bytes(),font_name=face.postscript_name.decode('ascii'))
                        assert actual==expected,(font.name,char,size,tm,sum(a!=b for a,b in zip(actual,expected)))
                        references+=1
            else:
                print('Pinned Latin page bitmaps: skipped on older CVT-scaling engines; authored cases still checked')
    print(f'PDF native hinting: {checks} page checks; {allocations} allocation/cache checks; {references} independent legacy-profile grayscale bitmaps')


if __name__=='__main__':main()
