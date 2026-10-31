#!/usr/bin/env python3
"""Check supplied annotation appearances without generating missing appearances."""
from pathlib import Path
import itertools
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_forms import form


def page(annotation=None, appearance=None, content='', extra=None, attrs='', resources='', annots='[10 0 R]'):
    objects = {10: annotation or '<< /Subtype /Square /Rect [3 1.5 9 10.5] /AP << /N 20 0 R >> >>',
               20: appearance or form('0 .5 1 rg 0 0 6 9 re f', '/BBox [0 0 6 9]')}
    objects.update(extra or {})
    return document('/MediaBox [0 0 18 12] /Annots '+annots+' /Resources << '+resources+' >> '+attrs,
                    stream=content, extra=objects)


def compressed_page(update=False, deleted=False):
    import zlib
    from test_pdf_xref import PDF
    from test_pdf_type3 import stream
    pdf=PDF()
    pdf.obj(1,b'<< /Type /Catalog /Pages 2 0 R >>')
    pdf.obj(2,b'<< /Type /Pages /Count 1 /Kids [3 0 R] >>')
    pdf.obj(3,b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Annots 30 0 R >>')
    pdf.obj(4,stream(''))
    pdf.obj(20,form('0 .5 1 rg 0 0 6 9 re f','/BBox [0 0 6 9] /Matrix 53 0 R'))
    pdf.obj(21,form('1 0 0 rg 0 0 6 9 re f','/BBox [0 0 6 9]'))
    objects={10:b'<< /Subtype /Widget /Rect 52 0 R /AP 31 0 R /AS /On >>',
             30:b'[null 10 0 R]',31:b'<< /N 32 0 R >>',32:b'<< /On 40 0 R /Off 21 0 R >>',
             40:b'20 0 R',52:b'[3 1.5 9 10.5]',53:b'[1 0 0 1 0 0]'}
    header=b'';body=b'';entries={}
    for index,(number,value) in enumerate(objects.items()):
        header+=f'{number} {len(body)} '.encode();body+=value+b' ';entries[number]=(2,55,index)
    pdf.obj(55,stream(zlib.compress(header+body),f'/Type /ObjStm /N {len(objects)} /First {len(header)} /Filter /FlateDecode'))
    previous=pdf.xref(100,{0:(0,0,65535),**pdf.objects,**entries})
    if update:
        pdf.obj(10,objects[10].replace(b'/On',b'/Off'))
        pdf.table({10:pdf.objects[10]},size=101,prev=previous)
    elif deleted:
        pdf.table({10:(0,0,1)},size=101,prev=previous)
    return bytes(pdf.data)


def main():
    probe, allocator = [str(Path(x).resolve()) for x in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupid-annotations-') as directory:
        path = Path(directory)/'case.pdf'
        def render(data, aa=False, reject=False):
            nonlocal checks
            path.write_bytes(data)
            options = ['--pdf-text-aa','--pdf-graphics-aa','--pdf-tt-hinting',
                       '--pdf-cff-hinting','--pdf-type1-hinting'] if aa else []
            result = subprocess.run([probe,str(path),'0',*options],capture_output=True,timeout=30)
            assert (result.returncode != 0) == reject, (result.returncode,result.stderr)
            checks += 1
            if reject:return None
            w,h,n = struct.unpack('<III',result.stdout[:12])
            assert n == 1 and len(result.stdout) == 12+w*h*4
            return w,h,result.stdout[12:]
        def fault(data, reject=False, aa=False):
            nonlocal allocations
            path.write_bytes(data)
            result = subprocess.run([allocator,str(path),str(int(reject)),*(['1']*5 if aa else [])],
                                    capture_output=True,text=True,check=True,timeout=120)
            allocations += int(result.stdout.split('checks')[0].split()[-1])
        white = bytes((255,255,255,255))*384
        expected = bytes(c for y in range(16) for x in range(24) for c in
                         ((0,128,255,255) if 4<=x<12 and 2<=y<14 else (255,255,255,255)))
        for aa in (False,True):
            assert render(page(),aa) == (24,16,expected)
            fault(page(),aa=aa)
            # No caller color, matrix, clipping, alpha, blend or mask leaks into AP.
            for dirty in ('1 0 0 rg', '2 0 0 2 30 30 cm', '0 0 1 1 re W n',
                          '/A gs', '/M gs', 'q 0 0 1 1 re W n'):
                extra={30:'<< /ca .25 /BM /Multiply >>',31:form('0 g 0 0 18 12 re f',
                       '/BBox [0 0 18 12] /Group << /S /Transparency /CS /DeviceGray /I true >>'),
                       32:'<< /SMask << /S /Luminosity /G 31 0 R >> >>'}
                assert render(page(content=dirty,extra=extra,resources='/ExtGState << /A 30 0 R /M 32 0 R >>'),aa)[2] == expected
            for subtype,flags in itertools.product(('Square','Widget','Stamp','Unknown'),(0,1,2,4,8,16,32,64,128,256,512)):
                annot=f'<< /Subtype /{subtype} /F {flags} /Rect [3 1.5 9 10.5] /AP << /N 20 0 R >> >>'
                want=white if flags & 34 or (subtype=='Unknown' and flags & 1) else expected
                assert render(page(annot),aa)[2] == want,(subtype,flags)
            assert render(page('<< /Subtype /Popup /Rect [3 1.5 9 10.5] /AP << /N 20 0 R >> >>'),aa)[2] == white
            for rect in ('[9 10.5 3 1.5]','[9 1.5 3 10.5]','[3 10.5 9 1.5]','40 0 R'):
                annot='<< /Subtype /Square /Rect '+rect+' /AP << /N 20 0 R >> >>'
                assert render(page(annot,extra={40:'[3 1.5 9 10.5]'}),aa)[2] == expected
            for field in ('/AP null','/AP << >>','/AP << /N null >>','/AP << /N << /On 20 0 R >> >>',
                          '/AS /Missing /AP << /N << /On 20 0 R >> >>', '/AP 999 0 R'):
                assert render(page('<< /Subtype /Widget /Rect [3 1.5 9 10.5] '+field+' >>'),aa)[2] == white
            # AS selection, escaped names, alias chains and all-direct dictionaries.
            for field in ('/AS /O#6e /AP << /N << /On 20 0 R >> >>',
                          '/AS /On /AP 30 0 R','/AP << /N 40 0 R >>'):
                extra={30:'<< /N 31 0 R >>',31:'<< /On 40 0 R /Off 99 0 R >>',
                       40:'41 0 R',41:'20 0 R'}
                assert render(page('<< /Subtype /Widget /Rect [3 1.5 9 10.5] '+field+' >>',extra=extra),aa)[2] == expected
            base=page(annots='[null 999 0 R 10 0 R]')
            assert render(base,aa)[2] == expected
            assert render(page(annots='[<< /Subtype /Square /Rect [3 1.5 9 10.5] /AP << /N 20 0 R >> >>]'),aa)[2] == expected
            for rect in ('[0 0 0 5]','[0 0 5 0]'):
                assert render(page('<< /Subtype /Square /Rect '+rect+' /AP << /N 20 0 R >> >>'),aa)[2] == white
            for box in ('[0 0 0 5]','[0 0 5 0]'):
                assert render(page(appearance=form('0 0 18 12 re f','/BBox '+box)),aa)[2] == white
            # BBox and Matrix define the rectangle mapping, not the raw content extent.
            for matrix in ('[2 0 0 3 17 -9]','[-1 0 0 -1 6 9]','[0 1 -1 0 9 0]',
                           '40 0 R','[41 0 R 0 0 42 0 R 17 -9]'):
                ap=form('0 .5 1 rg 0 0 6 9 re f','/BBox [0 0 6 9] /Matrix '+matrix)
                assert render(page(appearance=ap,extra={40:'[2 0 0 3 17 -9]',41:'2',42:'3'}),aa)[2] == expected,matrix
            assert render(page(appearance=form('0 .5 1 rg 0 0 6 9 re f','/BBox [0 0 6 9] /Matrix null')),aa)[2] == expected
            # Alpha belongs to the appearance, not the markup annotation's CA entry.
            for ca in (0,.25,1):
                annot=f'<< /Subtype /Highlight /CA {ca} /Rect [3 1.5 9 10.5] /AP << /N 20 0 R >> >>'
                assert render(page(annot),aa)[2] == expected
            # Page knockout must not erase the immediate annotation backdrop.
            for knockout in (False,True):
                attrs='/Group << /S /Transparency /CS /DeviceRGB /K '+str(knockout).lower()+' >>'
                ap=form('/A gs 1 0 0 rg 0 0 6 9 re f', '/BBox [0 0 6 9] /Resources << /ExtGState << /A << /ca .5 >> >> >>')
                actual=render(page(appearance=ap,content='0 0 1 rg 0 0 18 12 re f',attrs=attrs),aa)[2]
                want=bytes(c for y in range(16) for x in range(24) for c in
                           ((128,0,128,255) if 4<=x<12 and 2<=y<14 else (0,0,255,255)))
                assert actual == want,knockout
            # A hidden annotation and a hidden appearance both avoid stream execution.
            catalog='<< /Type /Catalog /Pages 2 0 R /OCProperties << /OCGs [90 0 R] /D << /OFF [90 0 R] >> >> >>'
            extra={1:catalog,90:'<< /Type /OCG /Name (off) >>'}
            annot='<< /Subtype /Square /Rect [3 1.5 9 10.5] /OC 90 0 R /AP << /N 20 0 R >> >>'
            assert render(page(annot,appearance=form('badOperator'),extra=extra),aa)[2] == white
            assert render(page(appearance=form('badOperator','/BBox [0 0 6 9] /OC 90 0 R'),extra=extra),aa)[2] == white
            # Form-local fonts execute through aliases and retain all text options.
            ap=form('BT /F 5 Tf 0 3 Td (AB) Tj ET', '/BBox [0 0 6 9] /Resources << /Font << /F 50 0 R >> >>')
            extra={50:'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',40:'20 0 R'}
            actual=render(page('<< /Subtype /Widget /Rect [3 1.5 9 10.5] /AP << /N 40 0 R >> >>',ap,extra=extra),aa)
            direct=document('/MediaBox [0 0 18 12] /Resources << /XObject << /G 20 0 R >> >>',
                            stream='1 0 0 1 3 1.5 cm /G Do',extra={20:ap,**extra})
            assert actual == render(direct,aa)
            fault(page(appearance=ap,extra=extra),aa=aa)
            # NoRotate fixes the upper-left anchor while keeping the appearance upright.
            # Pixel-center oracles avoid trigonometric round-off at right angles.
            for rotation, unit, no_rotate in itertools.product((0,90,180,270),(1,2),(False,True)):
                w,h=24*unit,16*unit
                left,top=8*unit,8*unit
                annot=f'<< /Subtype /Square /F {16 if no_rotate else 0} /Rect [6 3 12 6] /AP << /N 20 0 R >> >>'
                ap=form('1 0 0 rg 0 0 3 3 re f 0 1 0 rg 3 0 3 3 re f','/BBox [0 0 6 3]')
                output=render(page(annot,ap,attrs=f'/Rotate {rotation} /UserUnit {unit}'),aa)
                ow,oh=(h,w) if rotation in (90,270) else (w,h)
                want=bytearray(bytes((255,255,255,255))*(ow*oh))
                if no_rotate:
                    if rotation==90:left,top=h-top,left
                    elif rotation==180:left,top=w-left,h-top
                    elif rotation==270:left,top=top,w-left
                    for y in range(top,min(oh,top+4*unit)):
                        for x in range(left,min(ow,left+8*unit)):
                            want[(y*ow+x)*4:(y*ow+x+1)*4]=bytes((255,0,0,255) if x-left<4*unit else (0,255,0,255))
                else:
                    for y in range(top,top+4*unit):
                        for x in range(left,left+8*unit):
                            dx,dy=(h-1-y,x) if rotation==90 else (w-1-x,h-1-y) if rotation==180 else (y,w-1-x) if rotation==270 else (x,y)
                            want[(dy*ow+dx)*4:(dy*ow+dx+1)*4]=bytes((255,0,0,255) if x-left<4*unit else (0,255,0,255))
                assert output == (ow,oh,bytes(want)),(rotation,unit,no_rotate)
                if no_rotate:
                    assert render(page(annot.replace('/Square','/Text').replace('/F 16','/F 0'),ap,attrs=f'/Rotate {rotation} /UserUnit {unit}'),aa)==output
            # AP order and explicit group knockout/isolation remain separate from page K.
            for isolated,knockout in itertools.product((False,True),(False,True)):
                program='1 0 0 rg 0 0 6 9 re f /A gs 0 1 0 rg 0 0 6 9 re f'
                attrs='/BBox [0 0 6 9] /Group << /S /Transparency /CS /DeviceRGB /I '+str(isolated).lower()+' /K '+str(knockout).lower()+' >> /Resources << /ExtGState << /A << /ca .5 >> >> >>'
                ap=form(program,attrs)
                actual=render(page(appearance=ap,content='0 0 1 rg 0 0 18 12 re f'),aa)
                direct=document('/MediaBox [0 0 18 12] /Resources << /XObject << /G 20 0 R >> >>',stream='0 0 1 rg 0 0 18 12 re f 1 0 0 1 3 1.5 cm /G Do',extra={20:ap})
                assert actual == render(direct,aa),(isolated,knockout)
                fault(page(appearance=ap),aa=aa)
            second='<< /Subtype /Square /Rect [3 1.5 9 10.5] /AP << /N 21 0 R >> >>'
            second_ap=form('/A gs 1 0 0 rg 0 0 6 9 re f','/BBox [0 0 6 9] /Resources << /ExtGState << /A << /ca .5 >> >> >>')
            stacked=page(extra={11:second,21:second_ap},annots='[10 0 R 11 0 R]')
            want=bytes(c for y in range(16) for x in range(24) for c in
                       ((128,64,128,255) if 4<=x<12 and 2<=y<14 else (255,255,255,255)))
            assert render(stacked,aa)[2] == want
            assert render(page(extra={11:second,21:second_ap},annots='[11 0 R 10 0 R]'),aa)[2] == expected
            fault(stacked,aa=aa)
            # Clipping consumes the transformed BBox, including a sheared polygon.
            ap=form('0 .5 1 rg -100 -100 200 200 re f','/BBox [0 0 6 9] /Matrix [1 0 1 1 0 0]')
            mapped=page(appearance=ap)
            direct=document('/MediaBox [0 0 18 12] /Resources << /XObject << /G 20 0 R >> >>',
                            stream='.4 0 0 1 3 1.5 cm /G Do',extra={20:ap})
            assert render(mapped,aa) == render(direct,aa)
            fault(mapped,aa=aa)
            # Crop origins are in default user space and do not shift the AP anchor.
            cropped=document('/MediaBox [-3 -3 21 15] /CropBox [0 0 18 12] /Annots [10 0 R]',stream='',
                             extra={10:'<< /Subtype /Square /Rect [3 1.5 9 10.5] /AP << /N 20 0 R >> >>',
                                    20:form('0 .5 1 rg 0 0 6 9 re f','/BBox [0 0 6 9]')})
            assert render(cropped,aa)[2] == expected
        for bad in ('/F /Bad','/Rect [0 0 1]','/AP << /N 20 >>','/AP << /N 20 0 R >> /AS 7'):
            # AS is immaterial when N is a stream.
            annot='<< /Subtype /Square '+('' if bad.startswith('/Rect') else '/Rect [3 1.5 9 10.5] ')+('' if bad.startswith('/AP') else '/AP << /N 20 0 R >> ')+bad+' >>'
            if '/AS' in bad:continue
            render(page(annot),reject=True)
        for data in (page(extra={10:'10 0 R'}), page(extra={20:'21 0 R',21:'20 0 R'}),
                     page(extra={20:'21 0 R junk',21:form()}),
                     page(appearance=form('0 0 6 9 re f','/BBox [0 0 6 9] /Matrix [1 0 0]')),
                     page(appearance=form('/Self Do','/BBox [0 0 6 9] /Resources << /XObject << /Self 20 0 R >> >>'))):
            render(data,reject=True);fault(data,reject=True)
        for update,deleted in ((False,False),(True,False),(False,True)):
            data=compressed_page(update,deleted)
            wanted=white if deleted else bytes(c for y in range(16) for x in range(24) for c in
                        ((255,0,0,255) if 4<=x<12 and 2<=y<14 else (255,255,255,255))) if update else expected
            for aa in (False,True):assert render(data,aa)[2] == wanted
            fault(data)
        # The array count and metadata-work limits also apply when nothing is painted.
        assert render(page(annots='['+'null '*65536+']'))[2] == white
        render(page(annots='['+'null '*65537+']'),reject=True)
        large='<< /Subtype /Square /F 2 /Contents ('+'x'*512+') >>'
        render(page(large,annots='['+'10 0 R '*40000+']'),reject=True)
        empty=form('', '/BBox [0 0 6 9]')
        assert render(page(appearance=empty,annots='['+'10 0 R '*4096+']'))[2] == white
        render(page(appearance=empty,annots='['+'10 0 R '*4097+']'),reject=True)
        # The last legal alias depth resolves; a further hop fails before drawing.
        for depth in (16,17):
            objects={n:f'{n+1} 0 R' for n in range(100,100+depth-1)}
            objects[100+depth-1]=form('0 .5 1 rg 0 0 6 9 re f','/BBox [0 0 6 9]')
            data=page('<< /Subtype /Widget /Rect [3 1.5 9 10.5] /AP << /N 100 0 R >> >>',extra=objects)
            result=render(data,reject=depth==17)
            if depth==16:assert result[2] == expected
        from test_pdf_type3 import stream
        image=stream(bytes((0,128,255)), '/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB')
        pattern=stream('1 0 0 rg 0 0 1.5 3 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>')
        mask=form('1 g 0 0 3 9 re f','/BBox [0 0 6 9] /Group << /S /Transparency /CS /DeviceGray /I true >>')
        programs=[('6 0 0 9 0 0 cm /I Do','/XObject << /I 60 0 R >>',{60:image}),
                  ('/Pattern cs /P scn 0 0 6 9 re f','/Pattern << /P 60 0 R >>',{60:pattern}),
                  ('/M gs 0 .5 1 rg 0 0 6 9 re f','/ExtGState << /M << /SMask << /S /Alpha /G 60 0 R >> >> >>',{60:mask})]
        for content,resources,extra in programs:
            ap=form(content,'/BBox [0 0 6 9] /Resources << '+resources+' >>')
            data=page(appearance=ap,extra=extra)
            direct=document('/MediaBox [0 0 18 12] /Resources << /XObject << /G 20 0 R >> >>',
                            stream='1 0 0 1 3 1.5 cm /G Do',extra={20:ap,**extra})
            for aa in (False,True):assert render(data,aa)==render(direct,aa)
            fault(data,aa=True)
    print(f'PDF annotation appearances: {checks} page checks; {allocations} allocation checks')


if __name__=='__main__':main()
