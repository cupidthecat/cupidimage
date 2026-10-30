#!/usr/bin/env python3
"""Check bounded ICC table pipelines, PCS encodings and PDF source integration."""
import itertools
import math
import random
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_icc import profile as matrix_profile
from test_pdf_type3 import stream
from test_pdf_pages import document
from audit_pdf_corpus import read_ppm
from test_pdf_embedded_fonts import fixture as text_fixture
from test_pdf_mesh import fixture as mesh_fixture, VERTICES
from test_pdf_nchannel import space as process_space, function as process_function
from test_pdf_dct import image as jpeg_image


def lut(inputs=3,bits=16,grid=2,transform=None,input_tables=None,output_tables=None):
    maximum=(1<<bits)-1
    entries=256 if bits==8 else 2
    input_tables=input_tables or [[i/(entries-1) for i in range(entries)]]*inputs
    output_tables=output_tables or [[i/(entries-1) for i in range(entries)]]*3
    ins,outs=len(input_tables[0]),len(output_tables[0])
    assert all(len(t)==ins for t in input_tables) and all(len(t)==outs for t in output_tables)
    header=(b'mft1' if bits==8 else b'mft2')+bytes(4)+bytes([inputs,3,grid,0])
    header+=struct.pack('>9I',65536,0,0,0,65536,0,0,0,65536)
    if bits==16:header+=struct.pack('>HH',ins,outs)
    if transform is None:
        transform=(lambda x:(x[0],1-x[0],x[0])) if inputs==1 else (lambda x:(x[0],x[1]*x[2],x[0]*x[1]*x[2])) if inputs==3 else (lambda x:(x[0],x[1]*x[2],x[0]*x[3]))
    values=[v for table in input_tables for v in table]
    values += [v for point in itertools.product(range(grid),repeat=inputs) for v in transform([p/(grid-1) for p in point])]
    values += [v for table in output_tables for v in table]
    integers=[max(0,min(maximum,round(v*maximum))) for v in values]
    return header+(bytes(integers) if bits==8 else struct.pack('>'+str(len(integers))+'H',*integers))


def profile(table,inputs=3,lab=True,version=4,relative=None):
    base=matrix_profile(version=version)
    header=bytearray(base[:128])
    header[12:24]=b'scnr'+{1:b'GRAY',3:b'RGB ',4:b'CMYK'}[inputs]+(b'Lab ' if lab else b'XYZ ')
    tags=[]
    for i in range(struct.unpack_from('>I',base,128)[0]):
        tag,offset,length=struct.unpack_from('>4sII',base,132+i*12)
        if tag in (b'desc',b'cprt',b'wtpt'):tags.append((tag,base[offset:offset+length]))
    tags.append((b'A2B0',table))
    if relative is not None:tags.append((b'A2B1',relative))
    offset=132+12*len(tags);records=bytearray();data=bytearray()
    for tag,body in tags:
        records+=tag+struct.pack('>II',offset+len(data),len(body))
        data+=body;data+=bytes((-len(data))%4)
    result=header+struct.pack('>I',len(tags))+records+data
    result[:4]=struct.pack('>I',len(result))
    return bytes(result)


def fixture(data,values,inputs=3,range_value='',extra=None,content=None,resources='',spaces=''):
    objects={5:stream(data,'/N '+str(inputs)+' '+range_value)};objects.update(extra or {})
    return document('/MediaBox [0 0 18 12] /Resources << /ColorSpace << /S [/ICCBased 5 0 R] '+spaces+' >> '+resources+' >>',
                    stream=content or '/RelativeColorimetric ri /S cs '+' '.join(map(str,values))+' scn 0 0 18 12 re f',extra=objects)


def lab_values(bits,l=50,a=0,b=0):
    return [l/100*65280/65535,(a+128)*256/65535,(b+128)*256/65535] if bits==16 else [l/100,(a+128)/255,(b+128)/255]



def independent_lut(table,inputs,points):
    import ctypes as c
    import ctypes.util
    library=ctypes.util.find_library('lcms2')
    assert library, 'Optional independent color engine is unavailable'
    lib=c.CDLL(library)
    lib.cmsOpenProfileFromMem.argtypes=[c.c_void_p,c.c_uint32]
    lib.cmsOpenProfileFromMem.restype=c.c_void_p
    lib.cmsReadTag.argtypes=[c.c_void_p,c.c_uint32];lib.cmsReadTag.restype=c.c_void_p
    lib.cmsPipelineEvalFloat.argtypes=[c.POINTER(c.c_float),c.POINTER(c.c_float),c.c_void_p]
    lib.cmsCloseProfile.argtypes=[c.c_void_p]
    raw=profile(table,inputs);buffer=c.create_string_buffer(raw)
    source=lib.cmsOpenProfileFromMem(buffer,len(raw));assert source
    try:
        pipeline=lib.cmsReadTag(source,0x41324230);assert pipeline
        values=[]
        for point in points:
            incoming=(c.c_float*inputs)(*point);outgoing=(c.c_float*3)()
            lib.cmsPipelineEvalFloat(incoming,outgoing,pipeline)
            values.append(list(outgoing))
        return values
    finally:
        lib.cmsCloseProfile(source)


def main():
    evaluator,probe,allocator=[str(Path(a).resolve()) for a in sys.argv[1:4]]
    checks=references=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-icc-lut-') as tmp:
        path=Path(tmp)/'table.bin';pdf=Path(tmp)/'case.pdf'
        def evaluate(data,values,bits=16,lab=True,reject=False):
            nonlocal checks
            path.write_bytes(data)
            result=subprocess.run([evaluator,str(path),str(len(values)),str(int(lab)),*map(str,values)],capture_output=True,text=True,timeout=30)
            assert (result.returncode!=0)==reject,(result.returncode,result.stderr)
            checks+=1
            return list(map(float,result.stdout.split())) if not reject else None
        def render(data,reject=False,reference=False):
            nonlocal checks,references
            pdf.write_bytes(data);result=subprocess.run([probe,str(pdf),'0'],capture_output=True,timeout=30)
            assert (result.returncode!=0)==reject,result.stderr
            checks+=1
            if reference and '--reference' in sys.argv:
                prefix=Path(tmp)/'reference'
                ref=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(pdf),str(prefix)],capture_output=True,timeout=30)
                assert ref.returncode==0,ref.stderr
                w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                actual=bytes(v for i,v in enumerate(result.stdout[12:]) if i%4!=3)
                error=max(abs(a-b) for a,b in zip(actual,rgb))
                assert (w,h)==(24,16) and error<=2,(error,actual[:3],rgb[:3])
                references+=1
            return result.stdout
        def fault(data):
            nonlocal allocations
            pdf.write_bytes(data);result=subprocess.run([allocator,str(pdf),'0'],capture_output=True,text=True,timeout=120)
            assert result.returncode==0,(result.stdout,result.stderr)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        for bits in (8,16):
            for inputs in (1,3,4):
                table=lut(inputs,bits)
                choices=[[-1]*inputs,[0]*inputs,[1]*inputs,[2]*inputs]
                choices += list(itertools.permutations((.2,.5,.8))) if inputs==3 else [[.25]*inputs,[.2,.4,.6,.8][:inputs]]
                for values in choices:
                    x=[max(0,min(1,v)) for v in values]
                    expected=[x[0],1-x[0],x[0]] if inputs==1 else [x[0],min(x[1],x[2]),min(x)] if inputs==3 else [x[0],min(x[1],x[2]),x[0]*x[3]]
                    actual=evaluate(table,values,bits)
                    assert max(abs(a-b) for a,b in zip(actual,expected))<1e-12,(inputs,bits,values,actual,expected)
                for value in ('nan','inf','-inf'):evaluate(table,[value]*inputs,bits,reject=True)
                constant=lab_values(bits)
                table=lut(inputs,bits,transform=lambda x:constant)
                maximum=(1<<bits)-1;encoded=[round(v*maximum)/maximum for v in constant]
                factor=65535/65280 if bits==16 else 1
                expected=[encoded[0]*100*factor,encoded[1]*255*factor-128,encoded[2]*255*factor-128]
                space='[/Lab << /WhitePoint ['+str(0xf6d6/65536)+' 1 '+str(0xd32d/65536)+'] /Range [-128 128 -128 128] >>]'
                direct=document('/MediaBox [0 0 18 12] /Resources << /ColorSpace << /S '+space+' >> >>',stream='/S cs '+' '.join(map(str,expected))+' scn 0 0 18 12 re f')
                for version in (2,4):
                    data=fixture(profile(table,inputs,True,version),[.25]*inputs,inputs)
                    assert render(data,reference=True)==render(direct)
                fault(data)
            evaluate(lut(3,bits),[.2,.3,.4],bits,lab=False,reject=bits==8)
        table=lut(3,16,input_tables=[[0,.25,1]]*3,output_tables=[[0,.5,1]]*3)
        actual=evaluate(table,[.25,.5,.75]);x=[.1250019073777371,.2500038147554742,.6250019073777371]
        assert max(abs(a-b) for a,b in zip(actual,[x[0],min(x[1],x[2]),min(x)]))<3e-5,actual
        maximum_tables=[[i/4095 for i in range(4096)]]
        table=lut(4,16,input_tables=maximum_tables*4,output_tables=maximum_tables*3)
        assert evaluate(table,[1,1,1,1])==[1,1,1]
        assert evaluate(table,[0,0,0,0])==[0,0,0]
        assert evaluate(lut(1,16,grid=255),[1])==[1,0,1]
        assert evaluate(lut(1,8,grid=255),[0],8)==[0,1,0]
        render(fixture(profile(lut(3,8),lab=False),[.25]*3),reject=True)
        table=lut(3)
        for offset,value,kind in ((4,1,'I'),(8,2,'B'),(9,4,'B'),(10,0,'B'),(10,1,'B'),(11,1,'B'),(12,65537,'I'),(48,1,'H'),(50,4097,'H')):
            bad=bytearray(table);struct.pack_into('>'+kind,bad,offset,value);evaluate(bad,[.2,.3,.4],reject=True)
        for length in (0,3,7,11,47,48,51,len(table)-1):evaluate(table[:length],[.2,.3,.4],reject=True)
        baseline=evaluate(table,[.2,.3,.4])
        for tail in (b'\0',b'\xcd'*10,b'extra padding'):
            assert evaluate(table+tail,[.2,.3,.4])==baseline
        for inputs in (1,3,4):
            xyz_values=[.25*(0xf6d6/65536),.25,.25*(0xd32d/65536)]
            normalized=[v*32768/65535 for v in xyz_values]
            table=lut(inputs,16,transform=lambda x:normalized)
            expected=[round(v*65535)/65535 for v in normalized]
            space='[/CalRGB << /WhitePoint ['+str(0xf6d6/65536)+' 1 '+str(0xd32d/65536)+'] /Matrix ['+' '.join(str(65535/32768 if i%4==0 else 0) for i in range(9))+'] >>]'
            direct=document('/MediaBox [0 0 18 12] /Resources << /ColorSpace << /S '+space+' >> >>',stream='/S cs '+' '.join(map(str,expected))+' scn 0 0 18 12 re f')
            data=fixture(profile(table,inputs,False),[.25]*inputs,inputs)
            assert render(data,reference=True)==render(direct);fault(data)
        if '--reference' in sys.argv:
            randomizer=random.Random(51751)
            for bits in (8,16):
                for inputs in (1,3,4):
                    for grid in (2,3,5):
                        transform=(lambda x:(.25+.5*x[0],.4+.2*x[0],.4+.2*x[0])) if inputs==1 else (lambda x:(.25+.5*x[0],.4+.2*x[1]*x[2],.4+.2*x[0]*x[-1]))
                        table=lut(inputs,bits,grid,transform=transform)
                        points=[[0]*inputs,[1]*inputs,[.5]*inputs]+[[randomizer.random() for _ in range(inputs)] for _ in range(12)]
                        for point,expected in zip(points,independent_lut(table,inputs,points)):
                            actual=evaluate(table,point,bits)
                            assert max(abs(a-b) for a,b in zip(actual,expected))<=4e-5,(inputs,bits,grid,point,actual,expected)
                            references+=1
        primary=lut(transform=lambda x:lab_values(16,50));alternate=lut(transform=lambda x:lab_values(16,80))
        # A constant relative table has a nonzero source black point; the independent
        # default compensates it. Keep the uncompensated tag-selection oracle separate.
        assert render(fixture(profile(alternate,relative=primary),[.25]*3))==render(fixture(profile(primary),[.25]*3))
        primary=lut(grid=3,transform=lambda x:lab_values(16,100*x[0]))
        alternate=lut(grid=3,transform=lambda x:lab_values(16,100*x[0]*x[0]))
        assert render(fixture(profile(alternate,relative=primary),[.5]*3),reference=True)==render(fixture(profile(primary),[.5]*3))
        # A four-component source with a defined neutral ramp exercises every caller.
        source=profile(lut(4,16,transform=lambda x:lab_values(16,100*(1-x[0]))),4)
        lab='[/Lab << /WhitePoint ['+str(0xf6d6/65536)+' 1 '+str(0xd32d/65536)+'] /Range [-128 128 -128 128] >>]'
        def neutral(value):
            return document('/MediaBox [0 0 18 12] /Resources << /ColorSpace << /L '+lab+' >> >>',stream='/L cs '+str(value)+' 0 0 scn 0 0 18 12 re f')
        expected=render(neutral(0))
        data=fixture(source,[1,0,0,0],4,range_value='/Range 6 0 R',extra={6:'[0 1 0 1 0 1 0 1]'})
        assert render(data,reference=True)==expected;fault(data)
        data=fixture(source,[],4,content='/I cs 1 scn 0 0 18 12 re f',spaces='/I [/Indexed [/ICCBased 5 0 R] 1 <00000000ff000000>]')
        assert render(data)==expected;fault(data)
        image=stream(bytes([0,255,128,64,255,0,128,64]),'/Type /XObject /Subtype /Image /Width 2 /Height 1 /BitsPerComponent 8 /ColorSpace [/ICCBased 5 0 R]')
        data=fixture(source,[],4,content='18 0 0 12 0 0 cm /Im Do',extra={6:image},resources='/XObject << /Im 6 0 R >>')
        direct=document('/MediaBox [0 0 18 12]',stream='0 g 9 0 9 12 re f')
        assert render(data,reference=True)==render(direct);fault(data)
        for progressive in (False,True):
            for marker in (None,0,2):
                image=stream(jpeg_image(4,progressive,marker),'/Type /XObject /Subtype /Image /Width 8 /Height 8 /BitsPerComponent 8 /ColorSpace [/ICCBased 5 0 R] /Filter /DCTDecode')
                data=fixture(source,[],4,content='18 0 0 12 0 0 cm /Im Do',extra={6:image},resources='/XObject << /Im 6 0 R >>')
                value=125 if marker==2 else 129
                assert render(data)==render(neutral(100*(1-value/255)))
                if progressive and marker==2:fault(data)
        mask=stream(bytes([128]),'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray /Matte [0 0 0 0]')
        image=stream(bytes([128])*4,'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace [/ICCBased 5 0 R] /SMask 7 0 R')
        data=fixture(source,[],4,content='18 0 0 12 0 0 cm /Im Do',extra={6:image,7:mask},resources='/XObject << /Im 6 0 R >>')
        assert render(data)==render(document('/MediaBox [0 0 18 12]',stream=str(127/255)+' g 0 0 18 12 re f'));fault(data)
        cell=stream('0 0 3 3 re f','/Type /Pattern /PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 3 3] /XStep 3 /YStep 3 /Resources << >>')
        data=fixture(source,[],4,content='/P cs 1 0 0 0 /Tile scn 0 0 18 12 re f',spaces='/P [/Pattern [/ICCBased 5 0 R]]',extra={6:cell},resources='/Pattern << /Tile 6 0 R >>')
        assert render(data)==expected;fault(data)
        tint='[/Separation /Ink [/ICCBased 5 0 R] << /FunctionType 2 /Domain [0 1] /C0 [0 0 0 0] /C1 [1 0 0 0] /N 1 >>]'
        assert render(fixture(source,[],4,content='/Spot cs 1 scn 0 0 18 12 re f',spaces='/Spot '+tint))==expected
        names=('Cyan','Magenta','Yellow','Black')
        process=process_space(names,'[/ICCBased 5 0 R]',names)
        data=fixture(source,[],4,content='/Process cs 1 0 0 0 scn 0 0 18 12 re f',spaces='/Process '+process,extra={10:process_function(4)})
        assert render(data)==expected;fault(data)
        assert render(fixture(source,[],4,content='1 0 0 0 k 0 0 18 12 re f',spaces='/DefaultCMYK [/ICCBased 5 0 R]'))==expected
        page={3:'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> /ColorSpace << /S [/ICCBased 10 0 R] >> >> >>',10:stream(source,'/N 4')}
        data=text_fixture('/S cs 1 0 0 0 scn BT /F 30 Tf 20 30 Td (A) Tj ET',extra=page)
        assert render(data)==render(text_fixture('20 30 12 18 re f'));fault(data)
        vertices=[(flag,x,y,(1,0,0,0)) for flag,x,y,_ in VERTICES]
        direct_vertices=[(flag,x,y,(0,0,0)) for flag,x,y,_ in VERTICES]
        assert render(mesh_fixture(vertices=vertices,space='[/ICCBased 10 0 R]',extra={10:stream(source,'/N 4')}))==render(mesh_fixture(vertices=direct_vertices))
        shade='<< /ShadingType 2 /ColorSpace [/ICCBased 5 0 R] /Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [0 0 0 0] /C1 [1 0 0 0] /N 1 >> /Extend [true true] >>'
        expected_shade='<< /ShadingType 2 /ColorSpace '+lab+' /Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [100 0 0] /C1 [0 0 0] /N 1 >> /Extend [true true] >>'
        data=fixture(source,[],4,content='/Sh sh',resources='/Shading << /Sh 6 0 R >>',extra={6:shade})
        expected_data=fixture(source,[],4,content='/Sh sh',resources='/Shading << /Sh 6 0 R >>',extra={6:expected_shade})
        actual=render(data);oracle=render(expected_data)
        assert actual[:12]==oracle[:12] and max(abs(a-b) for a,b in zip(actual[12:],oracle[12:]))<=1
        fault(data)
        render(fixture(profile(lut(4),4),[.25]*3,3),reject=True)
        render(fixture(profile(lut(4),4),[.25]*4,4,range_value='/Range [0 1 0 1 0 1 0 2]'),reject=True)
    print(f'PDF ICC table pipelines: {checks} checks; {references} independent comparisons; {allocations} allocation checks')


if __name__=='__main__':main()
