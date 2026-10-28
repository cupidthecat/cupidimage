#!/usr/bin/env python3
"""Check font resource scope and ToUnicode filters without font-engine claims."""
import base64
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_lzw import literals


def stream(payload, attrs=''):
    return f'<< /Length {len(payload)} {attrs} >>\nstream\n'.encode()+payload+b'\nendstream'


def main():
    probe=str(Path(sys.argv[1]).resolve());count=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-font-maps-') as directory:
        def render(name,pdf):
            nonlocal count
            count+=1
            path=Path(directory)/(name+'.pdf');path.write_bytes(pdf)
            run=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=15)
            assert run.returncode==0,(name,run.stderr)
            assert any(c!=255 for c in run.stdout[12:]),name
            return run.stdout
        text='BT /F1 6 Tf 0 6 Td (Z) Tj ET'
        page='/MediaBox [0 0 18 12]'
        baseline=render('baseline-A',document(page,stream=text.replace('(Z)','(A)')))
        fallback=render('baseline-Z',document(page,stream=text))
        assert baseline!=fallback
        cmap=b'1 beginbfchar\n<5A> <0041>\nendbfchar'
        font='<< /Type /Font /Subtype /Type1 /ToUnicode 7 0 R >>'
        resources='/Resources << /Font << /F1 6 0 R >> >>'
        for label,payload,filters in (
            ('raw',cmap,''),('flate',zlib.compress(cmap),'/Filter /FlateDecode'),
            ('hex',cmap.hex().encode()+b'>','/Filter /ASCIIHexDecode'),
            ('ascii85',base64.a85encode(cmap)+b'~>','/Filter /ASCII85Decode'),
            ('lzw',literals(cmap,1),'/Filter /LZWDecode'),
            ('chain',base64.a85encode(zlib.compress(cmap))+b'~>','/Filter [/ASCII85Decode /FlateDecode]'),
        ):
            objects={6:font,7:stream(payload,filters)}
            for inherited in (False,True):
                pdf=document(page+(''if inherited else ' '+resources),resources if inherited else '',text,objects)
                assert render(label+str(inherited),pdf)==baseline
        objects={6:font,7:stream(cmap)}
        for name,res,extra in (
            ('indirect-resources','/Resources 8 0 R',{8:'<< /Font << /F1 6 0 R >> >>'}),
            ('indirect-font-map','/Resources << /Font 8 0 R >>',{8:'<< /F1 6 0 R >>'}),
            ('inline-font','/Resources << /Font << /F1 '+font+' >> >>',{}),
            ('comment','/Resources << /Font << % /Fake 99 0 R\n /F1 6 0 R >> >>',{}),
            ('nested-decoy','/Resources << /Nested << /Font << /F1 99 0 R >> >> /Font << /F1 6 0 R >> >>',{}),
            ('string-decoy','/Resources << /Note (>> /Font << /F1 99 0 R >>) /Font << /F1 6 0 R >> >>',{}),
            ('dictionary-name-value','/Resources << /Mode /Font /Font << /F1 6 0 R >> >>',{}),
        ):
            assert render(name,document(page+' '+res,stream=text,extra={**objects,**extra}))==baseline,name
        assert render('resources-shadow',document(page+' /Resources << >>',resources,text,objects))==fallback
        assert render('font-map-decoy',document(page+' /Resources << /Nested << /Font << /F1 6 0 R >> >> >>',stream=text,extra=objects))==fallback
        fake_font='<< /Type /Font /Nested << /ToUnicode 99 0 R >> /ToUnicode 7 0 R >>'
        assert render('ToUnicode-scope',document(page+' '+resources,stream=text,extra={**objects,6:fake_font}))==baseline
        fake_stream=stream(cmap,'/Note (/FlateDecode) /Nested << /Filter /FlateDecode >>')
        assert render('filter-scope',document(page+' '+resources,stream=text,extra={**objects,7:fake_stream}))==baseline
        # Invalid metadata retains the documented bitmap fallback, not a partial map.
        for name,body in (('bad-filter',stream(cmap,'/Filter /Missing')),('bad-length',b'<< /Length 1 >>\nstream\n'+cmap+b'\nendstream')):
            assert render(name,document(page+' '+resources,stream=text,extra={**objects,7:body}))==fallback
        for label,payload in (
            ('truncated-char',b'1 beginbfchar <5A> <0041>'),
            ('truncated-range',b'1 beginbfrange <5A> <5A> <0041>'),
            ('short-range-array',b'1 beginbfrange <5A> <5B> [<0041>] endbfrange'),
            ('unclosed-range-array',b'1 beginbfrange <5A> <5A> [<0041> endbfrange'),
            ('invalid-char-code',b'1 beginbfchar <5G> <0041> endbfchar'),
            ('range-too-large',b'1 beginbfrange <00000000> <fffffffe> <0041> endbfrange'),
            ('range-overflow',b'1 beginbfrange <00000000> <ffffffff> <0041> endbfrange'),
            ('array-range-too-large',b'1 beginbfrange <00000000> <ffffffff> [<0041>] endbfrange'),
        ):
            assert render(label,document(page+' '+resources,stream=text,extra={**objects,7:stream(payload)}))==fallback
        duplicate=b'4 beginbfchar <5A> <0042> <02> <0043> <5A> <0041> <01> <0044> endbfchar'
        assert render('duplicates-last-wins',document(page+' '+resources,stream=text,extra={**objects,7:stream(duplicate)}))==baseline
        full=b'1 beginbfrange <0000> <ffff> <0000> endbfrange'
        assert render('full-16-bit-map',document(page+' '+resources,stream=text.replace('(Z)','<0041>'),extra={**objects,7:stream(full)}))==baseline
        last=b'1 beginbfrange <ffffffff> <ffffffff> [<0041>] endbfrange'
        assert render('last-source-code',document(page+' '+resources,stream=text.replace('(Z)','<ffffffff>'),extra={**objects,7:stream(last)}))==baseline
        for size in (256,257):
            many='/Resources << /Font << '+' '.join(f'/F{i} 6 0 R'for i in range(size))+' >> >>'
            data = document(page+' '+many,stream=text,extra=objects)
            if size == 256:
                assert render('font-budget-'+str(size), data) == baseline
            else:
                path = Path(directory) / 'font-budget.pdf'
                path.write_bytes(data)
                rejected = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=15)
                assert rejected.returncode == 3, rejected.stderr
                count += 1
    print(f'PDF font-map resources: {count} passed')


if __name__=='__main__':main()
