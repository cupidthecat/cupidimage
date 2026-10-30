#!/usr/bin/env python3
"""Check ICC v4 A-to-B stages, shared curves and nonuniform CLUT grids."""
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
from test_pdf_icc import curve, gamma, parametric, response
from test_pdf_icc_lut import profile, fixture, independent_lut
from test_pdf_intents import tags
from test_pdf_type3 import stream
from test_pdf_pages import document

IDENTITY = (1,0,0,0,1,0,0,0,1,0,0,0)


def clut(inputs=3, bits=16, grids=None, transform=None):
    grids=grids or [2]*inputs
    transform=transform or (lambda x: (x[0],1-x[0],x[0]) if inputs==1 else
                           (x[0],x[1]*x[2],x[0]*x[-1]))
    values=[v for point in itertools.product(*[range(n) for n in grids])
            for v in transform([value/(grids[i]-1) for i,value in enumerate(point)])]
    maximum=(1<<bits)-1
    encoded=[round(max(0,min(1,v))*maximum) for v in values]
    return bytes(grids+[0]*(16-inputs))+bytes([bits//8,0,0,0])+(
        bytes(encoded) if bits==8 else struct.pack('>'+str(len(encoded))+'H',*encoded))


def mab(inputs=3, a=None, table=None, m=None, matrix=None, b=None, shared=False, order=(0,1,2,3,4)):
    def curves(values):return b''.join(v+bytes(-len(v)%4) for v in values)
    bodies=[curves(b or [curve()]*3),
            struct.pack('>12i',*[round(v*65536) for v in matrix]) if matrix is not None else None,
            curves(m) if m is not None else None,
            table, curves(a) if a is not None else None]
    header=bytearray(b'mAB '+bytes(4)+bytes([inputs,3,0,0])+bytes(20));payload=bytearray();seen={}
    for i in order:
        body=bodies[i]
        if body is None:continue
        if shared and body in seen:offset=seen[body]
        else:
            offset=32+len(payload);seen[body]=offset;payload+=body+bytes(-len(body)%4)
        struct.pack_into('>I',header,12+i*4,offset)
    return bytes(header+payload)


def main():
    evaluator,probe,allocator=[str(Path(a).resolve()) for a in sys.argv[1:4]]
    checks=references=allocations=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-icc-mab-') as tmp:
        table_path=Path(tmp)/'table.bin';pdf=Path(tmp)/'case.pdf'
        def evaluate(table,values,reject=False):
            nonlocal checks
            table_path.write_bytes(table)
            result=subprocess.run([evaluator,str(table_path),str(len(values)),'1',*map(str,values)],capture_output=True,text=True,timeout=30)
            assert (result.returncode!=0)==reject,(result.returncode,result.stderr)
            checks+=1
            return list(map(float,result.stdout.split())) if not reject else None
        def render(data,reject=False):
            nonlocal checks
            pdf.write_bytes(data);result=subprocess.run([probe,str(pdf),'0'],capture_output=True,timeout=30)
            assert (result.returncode!=0)==reject,(result.returncode,result.stderr)
            checks+=1
            return result.stdout
        def fault(data):
            nonlocal allocations
            pdf.write_bytes(data);result=subprocess.run([allocator,str(pdf),'0'],capture_output=True,text=True,timeout=120)
            assert result.returncode==0,(result.stdout,result.stderr)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        # Every allowed processing-element combination, including shared offsets.
        curves=[curve(),gamma(2),curve([0,12000,65535]),parametric(0,[2]),
                parametric(1,[2,1,.1]),parametric(2,[2,1,.1,.05]),
                parametric(3,[2,1,0,1,.25]),parametric(4,[2,1,0,1,.25,.1,.05])]
        for item in curves:
            table=mab(b=[item]*3)
            for values in ((0,0,0),(.1,.5,.9),(1,1,1),(-1,2,.25)):
                actual=evaluate(table,values)
                expected=[response(item,v) for v in values]
                assert max(abs(a-b) for a,b in zip(actual,expected))<1e-12,(actual,expected)
        matrix=(0,1,0,1,0,0,0,0,1,.125,-.25,.75)
        table=mab(m=[gamma(2)]*3,matrix=matrix,b=[gamma(2)]*3)
        for values in ((0,0,0),(.2,.5,.8),(1,1,1)):
            x=[v*v for v in values];expected=[max(0,min(1,x[1]+.125))**2,max(0,min(1,x[0]-.25))**2,max(0,min(1,x[2]+.75))**2]
            assert max(abs(a-b) for a,b in zip(evaluate(table,values),expected))<1e-12
        compare_matrix=(0,.5,0,.5,0,0,0,0,.5,.125,.125,.125)
        randomizer=random.Random(467)
        for inputs in (1,3,4):
            for bits in (8,16):
                for grids in ([2]*inputs,list(range(2,2+inputs))):
                    for with_matrix in (False,True):
                        table=mab(inputs,a=[gamma(2)]*inputs,table=clut(inputs,bits,grids),
                                  m=[curve()]*3 if with_matrix else None,
                                  matrix=compare_matrix if with_matrix else None,b=[gamma(2)]*3)
                        points=[[0]*inputs,[1]*inputs]+[[randomizer.random() for _ in range(inputs)] for _ in range(12)]
                        actual=[evaluate(table,point) for point in points]
                        if '--reference' in sys.argv:
                            expected=independent_lut(table,inputs,points)
                            for a,b in zip(actual,expected):
                                assert max(abs(x-y) for x,y in zip(a,b))<6e-5,(inputs,bits,grids,with_matrix,a,b)
                                references+=1
                        reordered=mab(inputs,a=[gamma(2)]*inputs,table=clut(inputs,bits,grids),
                                      m=[curve()]*3 if with_matrix else None,matrix=compare_matrix if with_matrix else None,
                                      b=[gamma(2)]*3,shared=True,order=(4,3,2,1,0))
                        assert evaluate(reordered,points[2])==actual[2]
        shared=mab(a=[curve()]*3,table=clut(),m=[curve()]*3,matrix=IDENTITY,b=[curve()]*3,shared=True)
        assert evaluate(shared,[.25,.5,.75])==[.25,.5,.25]
        for value in ('nan','inf','-inf'):evaluate(shared,[value]*3,reject=True)
        # Bounds, prohibited combinations, reserved fields and stage overlap.
        for cut in range(len(shared)):evaluate(shared[:cut],[.5]*3,reject=True)
        def changed(data,offset,value):
            result=bytearray(data);result[offset:offset+len(value)]=value;return bytes(result)
        for offset,value in [(0,b'mBA '),(4,b'bad!'),(8,b'\x02'),(9,b'\x04'),(10,b'\x01'),(11,b'\x01')]:
            evaluate(changed(shared,offset,value),[.5]*3,reject=True)
        for stage in range(5):
            for offset in (1,28,33,len(shared)+4,0xfffffffc):
                evaluate(changed(shared,12+stage*4,struct.pack('>I',offset)),[.5]*3,reject=True)
        for stage in (0,1,2,3,4):evaluate(changed(shared,12+stage*4,bytes(4)),[.5]*3,reject=True)
        offsets=struct.unpack_from('>5I',shared,12)
        evaluate(changed(shared,16,struct.pack('>I',offsets[0])),[.5]*3,reject=True)
        for offset in (0,1,2):evaluate(changed(shared,offsets[3]+offset,b'\x01'),[.5]*3,reject=True)
        for offset in (*range(3,16),17,18,19):evaluate(changed(shared,offsets[3]+offset,b'\x01'),[.5]*3,reject=True)
        for precision in (0,3,4,255):evaluate(changed(shared,offsets[3]+16,bytes([precision])),[.5]*3,reject=True)
        padded=mab(b=[gamma(2)]*3);off=struct.unpack_from('>I',padded,12)[0]
        evaluate(changed(padded,off+14,b'\x01'),[.5]*3,reject=True)
        reference_profiles=[]
        # New PCS Lab uses the full 16-bit range, unlike legacy lut16 Lab.
        constant=(.5,128/255,128/255)
        for inputs in (1,3,4):
            for bits in (8,16):
                table=mab(inputs,a=[curve()]*inputs,table=clut(inputs,bits,transform=lambda x:constant))
                raw=profile(table,inputs)
                profile_path=Path(tmp)/f'mab-{inputs}-{bits}.icc';profile_path.write_bytes(raw);reference_profiles.append(profile_path)
                data=fixture(raw,[.25]*inputs,inputs)
                actual=render(data);assert max(abs(v-119) for v in actual[12:15])<=1
                fault(data)
                render(fixture(profile(table,inputs,version=2),[.25]*inputs,inputs),reject=True)
                image=stream(bytes([128]*inputs),'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace [/ICCBased 5 0 R]')
                assert render(fixture(raw,[.25]*inputs,inputs,extra={6:image},resources='/XObject << /I 6 0 R >>',content='q 18 0 0 12 0 0 cm /I Do Q'))==actual
        # Source callers share the new stages without flattening their source space.
        source_table=mab(4,a=[curve()]*4,table=clut(4,16,[2,3,4,5],lambda x:(.5,128/255,128/255)))
        source=profile(source_table,4)
        expected=render(fixture(source,[.5]*4,4))
        cases=[]
        cases.append(fixture(source,[],4,spaces='/I [/Indexed [/ICCBased 5 0 R] 0 <80808080>]',content='/I cs 0 scn 0 0 18 12 re f'))
        fn='<< /FunctionType 2 /Domain [0 1] /C0 [0 0 0 0] /C1 [1 1 1 1] /N 1 >>'
        cases.append(fixture(source,[],4,spaces='/T [/Separation /Ink [/ICCBased 5 0 R] '+fn+']',content='/T cs .5 scn 0 0 18 12 re f'))
        from test_pdf_nchannel import space as process_space, function as process_function
        names=('Cyan','Magenta','Yellow','Black')
        process=process_space(names,'[/ICCBased 5 0 R]',names)
        cases.append(fixture(source,[],4,spaces='/P '+process,extra={10:process_function(4)},content='/P cs .2 .4 .6 .8 scn 0 0 18 12 re f'))
        cases.append(fixture(source,[],4,spaces='/DefaultCMYK [/ICCBased 5 0 R]',content='.2 .4 .6 .8 k 0 0 18 12 re f'))
        cell=stream(b'/S cs .2 .4 .6 .8 scn 0 0 18 12 re f','/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 18 12] /XStep 18 /YStep 12 /Resources << /ColorSpace << /S [/ICCBased 5 0 R] >> >>')
        cases.append(fixture(source,[],4,extra={6:cell},resources='/Pattern << /P 6 0 R >>',content='/Pattern cs /P scn 0 0 18 12 re f'))
        shade='<< /ShadingType 2 /ColorSpace [/ICCBased 5 0 R] /Coords [0 0 18 0] /Extend [true true] /Function '+fn+' >>'
        cases.append(fixture(source,[],4,extra={6:shade},resources='/Shading << /Sh 6 0 R >>',content='/Sh sh'))
        for data in cases:
            assert render(data)==expected
            fault(data)
        font=b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'
        text=fixture(source,[],4,extra={6:font},resources='/Font << /F 6 0 R >>',content='/S cs .2 .4 .6 .8 scn BT /F 9 Tf 1 2 Td (ICC) Tj ET')
        actual=render(text)
        assert tuple(expected[12:15]) in set(tuple(actual[i:i+3]) for i in range(12,len(actual),4))
        fault(text)
        # XYZ values above 1 survive profile decoding until the output conversion.
        xyz=(1.5,.5,.1);normalized=tuple(v*32768/65535 for v in xyz)
        table=mab(a=[curve()]*3,table=clut(transform=lambda x:normalized))
        from test_pdf_icc_lut import lut
        old=profile(lut(transform=lambda x:normalized),lab=False)
        raw=profile(table,lab=False)
        profile_path=Path(tmp)/'mab-xyz.icc';profile_path.write_bytes(raw);reference_profiles.append(profile_path)
        assert render(fixture(raw,[.5]*3))==render(fixture(old,[.5]*3))
        fault(fixture(raw,[.5]*3))
        # Profiles retain per-intent tables; selected unsupported stages still fail.
        perceptual=mab(b=[curve([20000,20000]),curve([32896,32896]),curve([32896,32896])])
        relative=mab(b=[curve([32640,32640]),curve([32896,32896]),curve([32896,32896])])
        raw=profile(perceptual,relative=relative)
        assert render(fixture(raw,[.5]*3))==render(fixture(profile(relative),[.5]*3))
        assert render(fixture(raw,[.5]*3,content='/Perceptual ri /S cs .5 .5 .5 scn 0 0 18 12 re f'))!=render(fixture(raw,[.5]*3))
        fault(fixture(raw,[.5]*3))
        reference_profiles.append(Path(tmp)/'mab-b-only.icc')
        reference_profiles[-1].write_bytes(raw)
        if '--reference' in sys.argv:
            comparison=Path(__file__).with_name('compare_pdf_icc_profiles.py')
            result=subprocess.run([sys.executable,str(comparison),probe,*map(str,reference_profiles)],capture_output=True,text=True,timeout=120)
            assert result.returncode==0,(result.stdout,result.stderr)
            references+=sum(row['samples'] for row in json.loads(result.stdout))
    print(f'PDF ICC A-to-B stages: {checks} checks; {references} independent comparisons; {allocations} allocation checks')


if __name__=='__main__':main()
