#!/usr/bin/env python3
"""Check PDF object token boundaries and filtered object-stream directories."""
import base64
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_lzw import literals
from audit_pdf_corpus import read_ppm

PAINT=b'1 0 0 rg 0 0 18 12 re f'


def stream(data, attrs=''):
    return f'<< /Length {len(data)} {attrs} >>\nstream\n'.encode()+data+b'\nendstream'


def pdf(objects=None, ws=b' ', separator=b'\n', compact=False, preamble=b'', compressed=None):
    objects = dict(objects or {1:b'<< /Type /Catalog /Pages 2 0 R >>',
                              2:b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
                              3:b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R >>',
                              4:stream(PAINT)})
    out=bytearray(b'%PDF-1.7\n'+preamble);offsets={}
    for number,body in sorted(objects.items()):
        offsets[number]=len(out)
        out+=str(number).encode()+ws+b'0'+ws+b'obj'+(b''if compact else b'\n')+body+b'\nendobj'+separator
    xref=len(out)
    if compressed is None:
        size=max(objects)+1
        out+=f'xref\n0 {size}\n0000000000 65535 f \n'.encode()
        for n in range(1,size):out+=f'{offsets.get(n,0):010} 00000 {"n" if n in offsets else "f"} \n'.encode()
        out+=f'trailer\n<< /Root 1 0 R /Size {size} >>\n'.encode()
    else:
        rows=bytearray()
        for n in range(10):
            if n in compressed:t,a,b=2,5,compressed.index(n)
            elif n==9:t,a,b=1,xref,0
            elif n in offsets:t,a,b=1,offsets[n],0
            else:t,a,b=0,0,65535 if n==0 else 0
            rows+=bytes([t])+struct.pack('>IH',a,b)
        out+=b'9 0 obj\n'+stream(rows,'/Type /XRef /Size 10 /Root 1 0 R /W [1 4 2]')+b'\nendobj\n'
    out+=f'startxref\n{xref}\n%%EOF\n'.encode()
    return bytes(out)


def main():
    probe=str(Path(sys.argv[1]).resolve());count=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-object-syntax-')as directory:
        def check(name,data,reject=False,reference=True):
            nonlocal count
            count+=1;path=Path(directory)/(name+'.pdf');path.write_bytes(data)
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert run.returncode==(3 if reject else 0),(name,run.stderr)
            if reject:return
            assert struct.unpack('=III',run.stdout[:12])==(24,16,1),name
            assert run.stdout[12:]==bytes((255,0,0,255))*384,name
            if '--reference'in sys.argv and reference:
                prefix=Path(directory)/'reference'
                subprocess.run(['pdftoppm','-r','96','-singlefile',str(path),str(prefix)],check=True,capture_output=True,timeout=15)
                assert read_ppm(prefix.with_suffix('.ppm'))==(24,16,bytes((255,0,0))*384),name
        for ws in (b' ',b'\t',b'\r',b'\n',b'\f',b'\0',b' % between tokens\n'):
            for compact in (False,True):
                check('header-'+str(count),pdf(ws=ws,separator=b' ',compact=compact))
        check('comment-decoy',pdf(preamble=b'% 1 0 obj << /Type /Catalog /Pages 99 0 R >> endobj\n'))
        for note in (b'(endobj)',b'(stream\n fake endobj)',b'<656e646f626a>',b'/endobj',b'(nested (endobj) text)',b'(escaped \\) endobj)'):
            objects={1:b'<< /Note '+note+b' /Type /Catalog /Pages 2 0 R >>',2:b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
                     3:b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R >>',4:stream(PAINT)}
            check('object-decoy-'+str(count),pdf(objects))
        bodies=[b'<< /Type /Catalog /Pages 2 0 R >>',b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
                b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R >>']
        offsets=[0,len(bodies[0])+1,len(bodies[0])+len(bodies[1])+2]
        header=' '.join(f'{i+1} {off}'for i,off in enumerate(offsets)).encode()+b' '
        decoded=header+b' '.join(bodies)
        rle=b''.join(bytes([len(decoded[i:i+128])-1])+decoded[i:i+128]for i in range(0,len(decoded),128))+b'\x80'
        for label,payload,filters in (('raw',decoded,''),('flate',zlib.compress(decoded),'/Filter /FlateDecode'),
                ('hex',decoded.hex().encode()+b'>','/Filter /ASCIIHexDecode'),
                ('ascii85',base64.a85encode(decoded)+b'~>','/Filter /ASCII85Decode'),
                ('lzw',literals(decoded,1),'/Filter /LZWDecode'),('runlength',rle,'/Filter /RunLengthDecode'),
                ('chain',base64.a85encode(zlib.compress(decoded))+b'~>','/Filter [/ASCII85Decode /FlateDecode]')):
            attrs=f'/Type /ObjStm /N 3 /First {len(header)} '+filters
            check('object-stream-'+label,pdf({4:stream(PAINT),5:stream(payload,attrs)},compressed=[1,2,3]))
        attrs=f'/Type /ObjStm /N 3 /First {len(header)}'
        check('object-stream-decoys',pdf({4:stream(PAINT),5:stream(decoded,'/Note (stream endobj) /Nested << /N 999 /First 999 /Filter /FlateDecode >> '+attrs)},compressed=[1,2,3]))
        for label,payload,properties in (
            ('short-header',decoded,attrs.replace('/First '+str(len(header)),'/First 1')),
            ('bad-count',decoded,attrs.replace('/N 3','/N 4')),
            ('nonmonotonic',b'1 0 2 0 3 1 '+b' '.join(bodies),'/Type /ObjStm /N 3 /First 12'),
            ('wrong-type',decoded,attrs.replace('/Type /ObjStm','/Note (/Type /ObjStm) /Type /Other')),
        ):
            check(label,pdf({4:stream(PAINT),5:stream(payload,properties)},compressed=[1,2,3]),reject=True)
    print(f'PDF object syntax: {count} passed')


if __name__=='__main__':main()
