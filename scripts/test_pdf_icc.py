#!/usr/bin/env python3
"""Authored ICC matrix/TRC profiles, native color oracles and malformed bounds."""
from pathlib import Path
import math
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_type3 import stream
from test_pdf_calibrated import M50
from audit_pdf_corpus import read_ppm
from test_pdf_xref import PDF


def fixed(value):
    return struct.pack('>i',round(value*65536))


def xyz(values):
    return b'XYZ '+bytes(4)+b''.join(fixed(v) for v in values)


def curve(values=()):
    return b'curv'+bytes(4)+struct.pack('>I',len(values))+b''.join(struct.pack('>H',v) for v in values)


def gamma(value):
    return curve([round(value*256)])


def parametric(kind,values):
    return b'para'+bytes(4)+struct.pack('>HH',kind,0)+b''.join(fixed(v) for v in values)


def mluc(text):
    value=text.encode('utf-16-be')
    return b'mluc'+bytes(4)+struct.pack('>II',1,12)+b'enUS'+struct.pack('>II',len(value),28)+value


def profile(curves=None,gray=False,lab=False,version=4,shared=True,extra=None):
    curves=curves or [curve()]*(1 if gray else 3)
    wp=(0xf6d6/65536,1,0xd32d/65536)
    matrix=list(map(float,M50.split()))
    name=b'CupidImage authored profile\0'
    desc=mluc(name[:-1].decode()) if version==4 else b'desc'+bytes(4)+struct.pack('>I',len(name))+name+bytes(78)
    copyright=mluc('Authored test fixture') if version==4 else b'text'+bytes(4)+b'Authored test fixture\0'
    tags=[(b'desc',desc),(b'cprt',copyright),(b'wtpt',xyz(wp))]
    if gray:
        tags.append((b'kTRC',curves[0]))
    else:
        for i,prefix in enumerate((b'r',b'g',b'b')):
            tags += [(prefix+b'XYZ',xyz(matrix[i*3:i*3+3])),(prefix+b'TRC',curves[i])]
    tags.extend(extra or [])
    header=bytearray(128)
    header[8:12]=bytes([version,0x30 if version==4 else 0x10,0,0])
    header[12:24]=b'mntr'+(b'GRAY' if gray else b'RGB ')+(b'Lab ' if lab else b'XYZ ')
    header[24:36]=struct.pack('>6H',2020,1,1,0,0,0)
    header[36:40]=b'acsp';header[64:68]=struct.pack('>I',1)
    header[68:80]=b''.join(fixed(v) for v in wp)
    records=bytearray();payload=bytearray();seen={};base=132+12*len(tags)
    for tag,data in tags:
        if shared and data in seen:
            offset=seen[data]
        else:
            offset=base+len(payload);seen[data]=offset
            payload+=data;payload+=bytes((-len(payload))%4)
        records+=tag+struct.pack('>II',offset,len(data))
    result=header+struct.pack('>I',len(tags))+records+payload
    result[:4]=struct.pack('>I',len(result))
    return bytes(result)


def response(data,x):
    x=max(0,min(1,x))
    if data[:4]==b'curv':
        n=struct.unpack_from('>I',data,8)[0]
        if not n:return x
        values=struct.unpack_from('>'+str(n)+'H',data,12)
        if n==1:return x**(values[0]/256)
        at=x*(n-1);i=int(at)
        return values[i]/65535 if i==n-1 else (values[i]+(at-i)*(values[i+1]-values[i]))/65535
    kind=struct.unpack_from('>H',data,8)[0]
    p=[v/65536 for v in struct.unpack_from('>'+str((len(data)-12)//4)+'i',data,12)]
    if kind==0:y=x**p[0]
    elif kind<3:y=((p[1]*x+p[2])**p[0] if x>=-p[2]/p[1] else 0)+(p[3] if kind==2 else 0)
    elif x>=p[4]:y=(p[1]*x+p[2])**p[0]+(p[5] if kind==4 else 0)
    else:y=p[3]*x+(p[6] if kind==4 else 0)
    return max(0,min(1,y))


def expected_space(gray=False,lab=False):
    wp=f'{0xf6d6/65536:.15f} 1 {0xd32d/65536:.15f}'
    if lab:return '[/Lab << /WhitePoint ['+wp+'] >>]'
    if gray:return '[/CalGray << /WhitePoint ['+wp+'] >>]'
    values=[round(float(v)*65536)/65536 for v in M50.split()]
    return '[/CalRGB << /WhitePoint ['+wp+'] /Matrix ['+' '.join(f'{v:.15f}' for v in values)+'] >>]'


def main():
    probe,allocator=[str(Path(arg).resolve()) for arg in sys.argv[1:3]]
    checks=allocations=references=0
    def fixture(data,content,gray=False,extra=None,metadata='',space='[/ICCBased 10 0 R]'):
        objects={10:stream(zlib.compress(data),f'/N {1 if gray else 3} /Filter /FlateDecode '+metadata)}
        objects.update(extra or {})
        return document('/MediaBox [0 0 18 12] /Resources << /ColorSpace << /C '+space+' >> /XObject << /Im 11 0 R /F 12 0 R >> >>',stream=content,extra=objects)
    def paint(values):return '/C cs '+' '.join(f'{v:.15f}' for v in values)+' sc 0 0 18 12 re f'
    with tempfile.TemporaryDirectory(prefix='cupidimage-icc-') as directory:
        path=Path(directory)/'case.pdf'
        def render(pdf,reject=False):
            nonlocal checks
            path.write_bytes(pdf)
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert run.returncode==(3 if reject else 0),run.stderr
            checks+=1
            return run.stdout
        def same(pdf,oracle,reference=False):
            nonlocal references
            actual=render(pdf);expected=render(oracle)
            assert actual==expected,'ICC profile transform differs from calibrated oracle'
            if reference and '--reference' in sys.argv:
                path.write_bytes(pdf);prefix=Path(directory)/'reference'
                run=subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=15)
                assert run.returncode==0,run.stderr
                w,h,rgb=read_ppm(prefix.with_suffix('.ppm'))
                assert (w,h)==(24,16) and max(abs(rgb[i]-actual[12+(i//3)*4+i%3]) for i in range(len(rgb)))<=2,(actual[12:16],rgb[:3])
                references+=1
        curves=[curve(),gamma(2.2),curve([0,4096,32768,65535]),curve([0,65535,0]),
                parametric(0,[2]),parametric(1,[2,1,-.2]),parametric(2,[2,1,-.2,.1]),
                parametric(3,[2,1,0,.5,.5]),parametric(4,[2,1,0,.5,.5,.1,.1]),
                parametric(3,[2.4,1/1.055,.055/1.055,1/12.92,.04045])]
        for gray in (False,True):
            for version in (2,4):
                for c in curves:
                    data=profile([c]*(1 if gray else 3),gray=gray,version=version)
                    for x in (0,.04,.25,.5,.75,1):
                        values=(x,) if gray else (x,.25,1-x)
                        decoded=tuple(response(c,v) for v in values)
                        same(fixture(data,paint(values),gray),fixture(data,paint(decoded),gray,space=expected_space(gray)),reference=c in (curve(),gamma(2.2)))
        # Maximum sampled-curve length includes the last sample without over-reading.
        maximum=profile([curve(range(65536))]*3)
        same(fixture(maximum,paint((0,.5,1))),fixture(maximum,paint((0,.5,1)),space=expected_space()))
        too_many=profile([curve([round(i*65535/65536) for i in range(65537)])]*3)
        render(fixture(too_many,paint((.2,.4,.6))),True)
        # Gray profiles can encode their achromatic PCS channel as Lab rather than XYZ.
        data=profile([gamma(2)],gray=True,lab=True)
        for x in (0,.1,.5,1):
            same(fixture(data,paint((x,)),True),fixture(data,paint((100*x*x,0,0)),True,space=expected_space(True,True)))
        data=profile([curve(),gamma(2),curve([0,65535])],shared=False)
        values=(.25,.5,.75);decoded=(.25,.25,.75)
        base=fixture(data,paint(values))
        same(base,fixture(data,paint(decoded),space=expected_space()))
        same(base,fixture(data,'/C cs .25 .5 .75 sc q 0 g Q 0 0 18 12 re f'))
        same(base,fixture(data,'/C CS .25 .5 .75 SC 12 w 0 6 m 18 6 l S'))
        same(base,fixture(data,paint(values),metadata='/Range null /Alternate /DeviceRGB'))
        same(base,fixture(data,paint(values),metadata='/Range 13 0 R',extra={13:'[0 1 0 1 0 1]'}))
        same(base,fixture(data,paint((.25,.5,.75)),space='[/ICCBased 13 0 R]',extra={13:'14 0 R',14:'10 0 R'}))
        indirect=stream(zlib.compress(data),'/N 13 0 R /Filter /FlateDecode')
        same(base,fixture(data,paint((.25,.5,.75)),extra={10:indirect,13:'3'}))
        render(fixture(data,paint((.25,.5,.75)),space='[/ICCBased 13 0 R]',extra={13:'14 0 R',14:'13 0 R'}),True)
        repeated=' '.join(['/C cs']*260)+' .25 .5 .75 sc 0 0 18 12 re f'
        same(base,fixture(data,repeated))
        sample=bytes([64,128,192]);values=tuple(v/255 for v in sample)
        baseline=fixture(data,paint(values))
        image=stream(sample,'/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace [/ICCBased 10 0 R]')
        same(baseline,fixture(data,'18 0 0 12 0 0 cm /Im Do',extra={11:image}))
        same(baseline,fixture(data,'18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /C /F /AHx ID 4080c0> EI'))
        same(baseline,fixture(data,'/C cs 0 sc 0 0 18 12 re f',space='[/Indexed [/ICCBased 10 0 R] 0 <4080c0>]'))
        form=stream('/C cs '+' '.join(str(v) for v in values)+' sc 0 0 18 12 re f','/Type /XObject /Subtype /Form /BBox [0 0 18 12] /Resources << /ColorSpace << /C [/ICCBased 10 0 R] >> >>')
        same(baseline,fixture(data,'/F Do /F Do',extra={12:form}))
        # Profile aliases, N, Range and the ColorSpace array can reside in object streams.
        packed={20:b'3',21:b'[0 1 0 1 0 1]',22:b'[/ICCBased 23 0 R]',23:b'10 0 R'}
        body=bytearray();header=''
        for number,value in packed.items():
            header+=f'{number} {len(body)} ';body+=value+b' '
        pdf=PDF()
        pdf.obj(1,b'<< /Type /Catalog /Pages 2 0 R >>')
        pdf.obj(2,b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>')
        pdf.obj(3,b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Resources << /ColorSpace << /C 22 0 R >> >> >>')
        pdf.obj(4,stream('18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /C /F /AHx ID 4080c0> EI'))
        pdf.obj(10,stream(zlib.compress(data),'/N 20 0 R /Range 21 0 R /Filter /FlateDecode'))
        pdf.obj(15,stream(zlib.compress(header.encode()+body),f'/Type /ObjStm /N 4 /First {len(header)} /Filter /FlateDecode'))
        entries={0:(0,0,65535),**pdf.objects}
        entries.update({number:(2,15,index) for index,number in enumerate(packed)})
        pdf.xref(30,entries=entries)
        compressed=bytes(pdf.data)
        same(baseline,compressed)
        # Reusing one object does not consume profile slots; distinct resources do.
        for count in (256,257):
            objects={100+i:stream(data,'/N 3') for i in range(count)}
            resources=' '.join(f'/C{i} [/ICCBased {100+i} 0 R]' for i in range(count))
            content=' '.join(f'/C{i} cs' for i in range(count))+' .25 .5 .75 sc 0 0 18 12 re f'
            bounded=document('/MediaBox [0 0 18 12] /Resources << /ColorSpace << '+resources+' >> >>',stream=content,extra=objects)
            if count==256:same(base,bounded)
            else:render(bounded,True)
        malformed=[]
        for offset,value in ((0,b'\0\0\0\0'),(8,b'\x03'),(12,b'link'),(16,b'CMYK'),(36,b'nope'),(68,bytes(4)),(128,b'\xff'*4),(136,b'\xff'*4)):
            altered=bytearray(data);altered[offset:offset+len(value)]=value;malformed.append(bytes(altered))
        malformed += [data[:100],data[:-1],profile([b'curv'+bytes(4)+struct.pack('>I',65537)+bytes(2)]*3),
                      profile([parametric(5,[1])]*3),profile([gamma(0)]*3),
                      profile([parametric(1,[2,0,0])]*3),profile([parametric(3,[.5,1,-1,1,0])]*3),
                      profile(extra=[(b'A2B0',b'mft1'+bytes(40))]),profile(extra=[(b'D2B0',b'mpet'+bytes(40))]),
                      profile(extra=[(b'rTRC',gamma(2))])]
        for broken in malformed:render(fixture(broken,paint((.2,.4,.6))),True)
        for metadata in ('/N 4','/N 1','/N null','/Range [0 1]','/Range [0 2 0 1 0 1]'):
            # Put the conflicting required entry first so dictionary precedence is explicit.
            altered=stream(zlib.compress(data),(metadata if metadata.startswith('/N ') else '/N 3 '+metadata)+' /Filter /FlateDecode')
            render(fixture(data,paint((.2,.4,.6)),extra={10:altered}),True)
        for pdf in (base,compressed,fixture(data,repeated),fixture(data,'18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /C /F /AHx ID 4080c0> EI')):
            path.write_bytes(pdf)
            run=subprocess.run([allocator,str(path),'0'],capture_output=True,text=True,timeout=60)
            assert run.returncode==0,run.stdout+run.stderr
            allocations+=int(run.stdout.split(': ')[1].split(' checks')[0])
    print(f'PDF ICC matrix/TRC: {checks} checks; {references} independent comparisons; {allocations} allocation checks')


if __name__=='__main__':
    main()
