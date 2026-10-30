#!/usr/bin/env python3
"""Check reverse ICC tables and processing-stage order using owned profiles."""
import itertools
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
from test_pdf_icc import curve, gamma, parametric, response, profile

IDENTITY=(1,0,0,0,1,0,0,0,1)

def clut(outputs=3,bits=16,grids=(2,2,2),transform=None):
    transform=transform or (lambda x: (x[0],min(x[1],x[2]),min(x),1-x[2])[:outputs])
    points=itertools.product(*[range(n) for n in grids])
    values=[v for p in points for v in transform([p[i]/(grids[i]-1) for i in range(3)])]
    maximum=(1<<bits)-1
    integers=[round(max(0,min(1,v))*maximum) for v in values]
    return bytes(integers) if bits==8 else struct.pack('>'+str(len(integers))+'H',*integers)

def lut(outputs=3,bits=16,matrix=IDENTITY,input_tables=None,output_tables=None):
    entries=256 if bits==8 else 2; maximum=(1<<bits)-1
    input_tables=input_tables or [[i/(entries-1) for i in range(entries)]]*3
    output_tables=output_tables or [[i/(entries-1) for i in range(entries)]]*outputs
    header=(b'mft1' if bits==8 else b'mft2')+bytes(4)+bytes((3,outputs,2,0))
    header+=struct.pack('>9i',*[round(x*65536) for x in matrix])
    if bits==16:header+=struct.pack('>HH',len(input_tables[0]),len(output_tables[0]))
    def encode(tables):
        values=[round(max(0,min(1,x))*maximum) for row in tables for x in row]
        return bytes(values) if bits==8 else struct.pack('>'+str(len(values))+'H',*values)
    return header+encode(input_tables)+clut(outputs,bits)+encode(output_tables)

def mba(outputs=3,b=None,matrix=None,m=None,table=None,a=None,shared=False,order=(0,1,2,3,4)):
    def curves(items):return b''.join(x+bytes(-len(x)%4) for x in items)
    bodies=[curves(b or [curve()]*3),
            struct.pack('>12i',*[round(v*65536) for v in matrix]) if matrix is not None else None,
            curves(m) if m is not None else None,table,curves(a) if a is not None else None]
    head=bytearray(b'mBA '+bytes(4)+bytes((3,outputs,0,0))+bytes(20));data=bytearray();seen={}
    for i in order:
        body=bodies[i]
        if body is None:continue
        if shared and body in seen:offset=seen[body]
        else:
            offset=32+len(data);seen[body]=offset;data+=body+bytes(-len(body)%4)
        struct.pack_into('>I',head,12+4*i,offset)
    return bytes(head+data)

def table(outputs,bits=16,grids=(2,2,2),transform=None):
    return bytes(list(grids)+[0]*13)+bytes((bits//8,0,0,0))+clut(outputs,bits,grids,transform)

def main():
    probe=str(Path(sys.argv[1]).resolve());checks=0
    rng=random.Random(4712)
    with tempfile.TemporaryDirectory(prefix='cupid-icc-reverse-') as directory:
        path=Path(directory)/'table.bin'
        def evaluate(data,outputs,values=(.2,.5,.8),kind='lut',lab=True,reject=False):
            nonlocal checks
            path.write_bytes(data)
            r=subprocess.run([probe,str(path),kind,str(outputs),str(int(lab)),*map(str,values)],capture_output=True,text=True,timeout=30)
            assert (r.returncode!=0)==reject,(r.returncode,r.stderr,kind,outputs,values)
            checks+=1
            return list(map(float,r.stdout.split())) if not reject else None
        points=[(-1,2,.5),(0,0,0),(1,1,1),(.5,.5,.5),*itertools.permutations((.2,.5,.8))]
        points += [tuple(rng.random() for _ in range(3)) for _ in range(100)]
        for outputs,bits in itertools.product((1,3,4),(8,16)):
            data=lut(outputs,bits)
            for point in points:
                x=[max(0,min(1,v)) for v in point]
                expected=(x[0],min(x[1],x[2]),min(x),1-x[2])[:outputs]
                actual=evaluate(data,outputs,point)
                assert max(abs(a-b) for a,b in zip(actual,expected))<1e-12,(actual,expected)
            for point in ((float('nan'),0,0),(0,float('inf'),0),(0,0,-float('inf'))):
                evaluate(data,outputs,point,reject=True)
            for end in range(len(data)):
                evaluate(data[:end],outputs,reject=True)
            if bits==8:evaluate(data,outputs,lab=False,reject=True)
        matrix=(0,.5,0,1,0,0,0,0,1)
        for outputs in (1,3,4):
            data=lut(outputs,matrix=matrix)
            for point in points:
                x=[max(0,min(1,v)) for v in point];x=(x[1]*.5,x[0],x[2])
                expected=(x[0],min(x[1],x[2]),min(x),1-x[2])[:outputs]
                actual=evaluate(data,outputs,point,lab=False)
                assert max(abs(a-b) for a,b in zip(actual,expected))<1e-12
            evaluate(data,outputs,lab=True,reject=True)
        # Every allowed mBA element combination; the forward stage order differs.
        for full,outputs,bits in itertools.product((False,True),(1,3,4),(8,16)):
            matrix=(*IDENTITY,.125,-.25,.25) if full else None
            data=mba(outputs,b=[gamma(2)]*3,matrix=matrix,m=[gamma(2)]*3 if full else None,
                     table=table(outputs,bits),a=[gamma(2)]*outputs)
            for point in points:
                x=[max(0,min(1,v))**2 for v in point]
                if full:x=[max(0,min(1,v+d))**2 for v,d in zip(x,(.125,-.25,.25))]
                expected=[v*v for v in (x[0],min(x[1],x[2]),min(x),1-x[2])[:outputs]]
                actual=evaluate(data,outputs,point,kind='mba')
                assert max(abs(a-b) for a,b in zip(actual,expected))<1e-12,(actual,expected)
            for end in range(len(data)):
                evaluate(data[:end],outputs,kind='mba',reject=(end!=len(data)-2))
        for value in (curve(),gamma(2),parametric(3,[2,1,0,1,.25])):
            for point in points:
                actual=evaluate(mba(b=[value]*3),3,point,kind='mba')
                expected=[response(value,max(0,min(1,v))) for v in point]
                assert max(abs(a-b) for a,b in zip(actual,expected))<1e-12
        data=mba(b=[gamma(2)]*3,matrix=(0,1,0,1,0,0,0,0,1,.125,-.25,.75),m=[gamma(2)]*3)
        for point in points:
            x=[max(0,min(1,v))**2 for v in point]
            expected=[max(0,min(1,v))**2 for v in (x[1]+.125,x[0]-.25,x[2]+.75)]
            assert max(abs(a-b) for a,b in zip(evaluate(data,3,point,kind='mba'),expected))<1e-12
        for outputs in (1,4):evaluate(mba(outputs),outputs,kind='mba',reject=True)
        # Stage offsets need not follow execution order, and curves can share
        # storage. Nonuniform grids still interpolate affine outputs exactly.
        for outputs in (1,3,4):
            data=mba(outputs,b=[gamma(2)]*3,matrix=(*IDENTITY,0,0,0),m=[gamma(2)]*3,
                     table=table(outputs,grids=(2,3,4),transform=lambda x: (x[0],x[1],x[2],sum(x)/3)[:outputs]),
                     a=[gamma(2)]*outputs,shared=True,order=(4,3,2,1,0))
            for point in points:
                x=[max(0,min(1,v))**4 for v in point]
                expected=[v*v for v in (x[0],x[1],x[2],sum(x)/3)[:outputs]]
                actual=evaluate(data,outputs,point,kind='mba')
                assert max(abs(a-b) for a,b in zip(actual,expected))<4/65535
        base=mba(table=table(3),a=[curve()]*3)
        for offset in (12,24,28):
            bad=bytearray(base);struct.pack_into('>I',bad,offset,0)
            evaluate(bad,3,kind='mba',reject=True)
        for offset,value in ((12,31),(24,33),(28,len(base)+4),(16,32)):
            bad=bytearray(base);struct.pack_into('>I',bad,offset,value)
            evaluate(bad,3,kind='mba',reject=True)
        for point in ((float('nan'),0,0),(0,float('inf'),0),(0,0,-float('inf'))):
            evaluate(base,3,point,kind='mba',reject=True)
        references=0
        if '--reference' in sys.argv:
            import ctypes as c
            import ctypes.util
            lib=c.CDLL(ctypes.util.find_library('lcms2'))
            lib.cmsOpenProfileFromMem.argtypes=[c.c_void_p,c.c_uint32];lib.cmsOpenProfileFromMem.restype=c.c_void_p
            lib.cmsReadTag.argtypes=[c.c_void_p,c.c_uint32];lib.cmsReadTag.restype=c.c_void_p
            lib.cmsPipelineEvalFloat.argtypes=[c.POINTER(c.c_float),c.POINTER(c.c_float),c.c_void_p]
            lib.cmsCloseProfile.argtypes=[c.c_void_p]
            for outputs,kind,bits in itertools.product((1,3,4),('lut','mba'),(8,16)):
                data=lut(outputs,bits) if kind=='lut' else mba(outputs,b=[gamma(2)]*3,
                    matrix=(*IDENTITY,.125,-.25,.25),m=[gamma(2)]*3,table=table(outputs,bits),a=[gamma(2)]*outputs)
                raw=bytearray(profile(gray=outputs==1,lab=True,extra=[(b'B2A0',data)]))
                if outputs==4:raw[12:20]=b'prtrCMYK'
                buffer=c.create_string_buffer(bytes(raw));handle=lib.cmsOpenProfileFromMem(buffer,len(raw));assert handle
                try:
                    pipeline=lib.cmsReadTag(handle,0x42324130);assert pipeline
                    for point in points[1:30]:
                        incoming=(c.c_float*3)(*point);outgoing=(c.c_float*outputs)()
                        lib.cmsPipelineEvalFloat(incoming,outgoing,pipeline)
                        actual=evaluate(data,outputs,point,kind=kind)
                        assert max(abs(a-b) for a,b in zip(actual,outgoing))<4/65535,(kind,outputs,bits,actual,list(outgoing))
                        references+=1
                finally:lib.cmsCloseProfile(handle)
    print(f'ICC reverse pipelines: {checks} checks; {references} independent comparisons')

if __name__=='__main__':main()
