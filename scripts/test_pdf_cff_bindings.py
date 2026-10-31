#!/usr/bin/env python3
"""Keep PDF CIDs, CFF charset SIDs and CharStrings indices distinct."""
import base64
import itertools
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_cff import fixture as cff_font
from test_pdf_cff_render import fixture as pdf, BOX, CURVE
from test_pdf_type2 import command
from test_pdf_type3 import stream
from test_pdf_forms import form


PROGRAMS = [b'\x0e', b'\x0e', BOX, CURVE]
DIRECT = cff_font(programs=PROGRAMS)
CID = cff_font(cid=True, charset=b'\0\0\1\0\2\0\3', programs=PROGRAMS)
OPENTYPE = base64.b64decode(json.loads(Path('tests/fixtures/pdf-cff-opentype.json').read_text())['font'])


def fixture(content, opentype=False, **kwargs):
    objects = dict(kwargs.pop('extra', {}))
    if opentype:
        objects[9] = stream(OPENTYPE, '/Subtype /OpenType')
    return pdf(content, cid=True, font=kwargs.pop('font', DIRECT), extra=objects, **kwargs).replace(b'%PDF-1.4', b'%PDF-1.7', 1)


def main():
    probe, allocator = [str(Path(x).resolve()) for x in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupid-cff-bindings-') as directory:
        path = Path(directory)/'case.pdf'
        def render(data, hinted=False, reject=False):
            nonlocal checks
            path.write_bytes(data)
            flags=['--pdf-text-aa','--pdf-graphics-aa','--pdf-tt-hinting','--pdf-cff-hinting','--pdf-type1-hinting'] if hinted else []
            result=subprocess.run([probe,str(path),'0',*flags],capture_output=True,timeout=30)
            assert result.returncode==(3 if reject else 0),(result.returncode,result.stderr)
            checks+=1
            if reject:return None
            w,h,n=struct.unpack('<III',result.stdout[:12])
            assert n==1 and len(result.stdout)==12+w*h*4
            return result.stdout
        def fault(data,hinted=False,reject=False):
            nonlocal allocations
            path.write_bytes(data)
            result=subprocess.run([allocator,str(path),str(int(reject)),*(['1']*5 if hinted else [])],capture_output=True,text=True,check=True,timeout=120)
            allocations+=int(result.stdout.split('checks')[0].split()[-1])
        for mode,hinted,vertical,opentype in itertools.product(range(8),(False,True),(False,True),(False,True)):
            codes='<00020002>' if opentype else '<000200030002>'
            content=f'1 0 0 rg 0 0 1 RG 1 w BT /F 18 Tf 25 55 Td {mode} Tr '+codes+' Tj ET'
            if mode>=4:content+=' 0 .5 1 rg 0 0 120 90 re f'
            metrics='/DW 600 /W [2 [800 900]] /DW2 [880 -1000] /W2 [2 [-1200 350 800 -1300 450 900]]'
            assert render(fixture(content,opentype,vertical=vertical,metrics=metrics),hinted)==render(pdf(content,cid=True,vertical=vertical,font=CID,metrics=metrics),hinted),(mode,hinted,vertical,opentype)
        for opentype,hinted in itertools.product((False,True),(False,True)):
            content='BT /F 30 Tf 18 30 Td <0002> Tj ET'
            data=fixture(content,opentype)
            if not hinted:
                assert render(data)==render(pdf('18 30 12 18 re f'))
            fault(data,hinted)
            # Charset SID 34 names GID 2; it is not a valid direct GID here.
            for code in (0,1,4,34,391,65535):
                assert render(fixture(f'BT /F 30 Tf 20 30 Td <{code:04x}> Tj ET',opentype),hinted)==render(pdf(''),hinted),code
            # Extraction text and CFF built-in Encoding do not select CID glyphs.
            top='<< /Type /Font /Subtype /Type0 /BaseFont /CupidShapes /Encoding /Identity-H /DescendantFonts [6 0 R] /ToUnicode 10 0 R >>'
            unicode=stream('1 beginbfchar <0002> <005A> endbfchar')
            assert render(fixture(content,opentype,extra={5:top,10:unicode}),hinted)==render(data,hinted)
            assert render(fixture(content,opentype,extra={5:top,10:stream('not a cmap')}),hinted)==render(data,hinted)
            # Missing direct GIDs consult the encoding's notdef map before CID zero.
            encoding=stream('1 begincodespacerange <0000> <ffff> endcodespacerange 1 begincidchar <0099> 65535 endcidchar 1 beginnotdefchar <0099> 2 endnotdefchar')
            top=top.replace('/Identity-H','10 0 R').replace(' /ToUnicode 10 0 R','')
            actual=fixture(content.replace('<0002>','<0099>'),opentype,extra={5:top,10:encoding},metrics='/DW 0 /W [2 [600] 65535 [10]]')
            assert render(actual,hinted)==render(data,hinted)
            fault(actual,hinted)
        for matrix in ((.002,0,0,.001,0,0),(.001,.0003,.0002,.001,.1,.2)):
            direct=cff_font(programs=PROGRAMS,matrix=matrix)
            control=cff_font(cid=True,charset=b'\0\0\1\0\2\0\3',programs=PROGRAMS,matrix=matrix)
            for hinted,vertical in itertools.product((False,True),(False,True)):
                content='BT /F 18 Tf 30 40 Td <00020003> Tj ET'
                assert render(fixture(content,font=direct,vertical=vertical),hinted)==render(pdf(content,cid=True,font=control,vertical=vertical),hinted)
        # A name-keyed program remains name-keyed internally: seac resolves StandardEncoding names.
        accent=command([0,0],21)+command([100,0,0,100,-100,0],5)+b'\x0e'
        composite=command([100,600,65,194],14)
        composed=cff_font(glyphs=5,charset=b'\0\0\1\0\x22\0\x7d\0\xab',strings=(),programs=[b'\x0e',b'\x0e',BOX,accent,composite])
        for hinted in (False,True):
            content='BT /F 30 Tf 20 30 Td <0004> Tj ET'
            actual=fixture(content,font=composed)
            control=pdf(content.replace('<0004>','(A)'),font=composed,encoding='<< /Differences [65 /Aacute] >>')
            assert render(actual,hinted)==render(control,hinted)
            fault(actual,hinted)
        # Explicit character spacing, word spacing, horizontal scaling, rise and TJ adjustments.
        for vertical,hinted in itertools.product((False,True),(False,True)):
            content='BT /F 18 Tf 1 Tc 7 Tw 75 Tz 2 Ts 25 55 Td [<0002> -125 <0003> 250 <0002>] TJ ET'
            assert render(fixture(content,vertical=vertical),hinted)==render(pdf(content,cid=True,font=CID,vertical=vertical),hinted)
        # Repeated Form-local fonts and page fonts may evict each other's prepared cache entries.
        child=form('BT /F 18 Tf 30 40 Td <0002> Tj ET','/BBox [0 0 120 90] /Resources << /Font << /F 10 0 R >> >>')
        top='<< /Type /Font /Subtype /Type0 /BaseFont /CupidShapes /Encoding /Identity-H /DescendantFonts [11 0 R] >>'
        descendant='<< /Type /Font /Subtype /CIDFontType0 /BaseFont /CupidShapes /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor 12 0 R /DW 600 >>'
        descriptor='<< /Type /FontDescriptor /FontName /CupidShapes /Flags 32 /FontBBox [0 0 1000 1000] /Ascent 800 /Descent -200 /CapHeight 600 /ItalicAngle 0 /StemV 80 /FontFile3 13 0 R >>'
        extra={3:'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> /XObject << /G 20 0 R >> >> >>',10:top,11:descendant,12:descriptor,13:stream(DIRECT,'/Subtype /CIDFontType0C'),20:child}
        content='/G Do BT /F 18 Tf 60 40 Td <0002> Tj ET /G Do'
        actual=pdf(content,cid=True,font=CID,extra=extra)
        extra[13]=stream(CID,'/Subtype /CIDFontType0C')
        assert render(actual,True)==render(pdf(content,cid=True,font=CID,extra=extra),True)
        fault(actual,True)
        # CID-keyed CFF still cannot be reinterpreted as a simple-font Encoding.
        bad=pdf('BT /F 18 Tf 20 30 Td (A) Tj ET',font=CID)
        render(bad,reject=True);fault(bad,reject=True)
        bad=fixture('BT /F 18 Tf 20 30 Td <0002> Tj ET',font=DIRECT[:-1])
        render(bad,reject=True);fault(bad,reject=True)
    print(f'PDF CFF CID bindings: {checks} page checks; {allocations} allocation checks')


if __name__=='__main__':main()
