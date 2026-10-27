#!/usr/bin/env python3
"""Inline images must match equivalent XObjects without scanning binary as operators."""
import base64
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document
from test_pdf_images import fixture
from test_pdf_lzw import literals
from test_pdf_packed_images import pack, encode
from test_jpeg_decoder import jpeg, scan, segment, EOI


def inline_document(content, resources='', extra=None):
    objects = dict(extra or {})
    objects[4] = b'<< /Length '+str(len(content)).encode()+b' >>\nstream\n'+content+b'\nendstream'
    return document('/MediaBox [0 0 18 12] '+resources, extra=objects)


def main():
    probe = str(Path(sys.argv[1]).resolve())
    count = references = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-inline-') as temporary:
        def render(data):
            path = Path(temporary)/'case.pdf'
            path.write_bytes(data)
            return subprocess.run([probe, str(path), '0'], capture_output=True, timeout=15)
        def check(name, data, attrs, shorthand=None, separator=b' ', suffix=b'\nEI', prefix=b'', resources='', extra=None):
            nonlocal count, references
            inline = (b'0.5 g -100 -100 300 300 re f '+prefix+b'18 0 0 12 0 0 cm BI '+
                      (shorthand or attrs).encode()+b' ID'+separator+data+suffix)
            actual = render(inline_document(inline, resources, extra))
            expected = render(fixture(data, attrs, content=prefix.decode()+'18 0 0 12 0 0 cm /Im0 Do', extra=extra))
            assert actual.returncode == expected.returncode == 0, (name, actual.stderr, expected.stderr)
            assert struct.unpack('=III', actual.stdout[:12]) == (24,16,1)
            assert actual.stdout == expected.stdout, (name, 'XObject comparison')
            if '--reference' in sys.argv and name in ('raw-DeviceRGB-8','flate-DeviceRGB-8','jpeg','indexed','named-resource','named-indexed','clip'):
                import fitz
                with fitz.open(stream=inline_document(inline,resources,extra),filetype='pdf') as reference:
                    image=reference[0].get_pixmap(matrix=fitz.Matrix(96/72,96/72),alpha=False)
                    rgb=bytes(v for i,v in enumerate(actual.stdout[12:]) if i%4!=3)
                    assert (image.width,image.height)==(24,16)
                    assert max(abs(a-b) for a,b in zip(rgb,image.samples))<=1,(name,'reference pixels')
                references+=1
            count += 1
        for bits in [1,2,4,8,16]:
            maximum=(1<<bits)-1
            for components,space,alias in [(1,'DeviceGray','G'),(3,'DeviceRGB','RGB'),(4,'DeviceCMYK','CMYK')]:
                data=b''.join(pack([(i*maximum)//(3*components-1) for i in range(3*components)][::direction],bits) for direction in [1,-1])
                attrs=f'/Width 3 /Height 2 /BitsPerComponent {bits} /ColorSpace /{space}'
                short=f'/W 3 /H 2 /BPC {bits} /CS /{alias}'
                check(f'raw-{space}-{bits}',data,attrs,short)
                check(f'hex-{space}-{bits}',data.hex().encode()+b'>',attrs+' /Filter /ASCIIHexDecode',short+' /F /AHx')
                check(f'a85-{space}-{bits}',base64.a85encode(data)+b'~>',attrs+' /Filter /ASCII85Decode',short+' /F /A85')
                rle=bytes([len(data)-1])+data+b'\x80'
                check(f'rle-{space}-{bits}',rle,attrs+' /Filter /RunLengthDecode',short+' /F /RL')
                flate=zlib.compress(data)
                check(f'flate-{space}-{bits}',flate,attrs+' /Filter /FlateDecode',short+' /F /Fl')
                check(f'chain-{space}-{bits}',flate.hex().encode()+b'>',attrs+' /Filter [/ASCIIHexDecode /FlateDecode]',short+' /F [/AHx /Fl]')
        for components in (1,3):
            space='DeviceGray' if components==1 else 'DeviceRGB'
            for progressive in (False,True):
                payload=jpeg(components,progressive,2,2)
                payload+=segment(0xFE,b' EI '+EOI+b' endstream ')
                payload+=scan(tuple(range(1,components+1)),se=0 if progressive else 63)
                payload+=((b'\x7f' if progressive else b'\x3f') if components==1 else b'\x1f' if progressive else b'\x03')+EOI
                attrs=f'/Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /{space}'
                check('jpeg',payload,attrs+' /Filter /DCTDecode',attrs+' /F /DCT')
        for predictor in (2,10,11,12,13,14,15):
            data=zlib.compress(encode([[0,128,255],[255,128,0]],1,8,predictor))
            attrs='/Width 3 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceGray /Filter /FlateDecode'
            params=f'<< /Predictor {predictor} /Columns 3 /Colors 1 /BitsPerComponent 8 >>'
            check('predictor',data,attrs+' /DecodeParms '+params,attrs+' /DP '+params)
        gray='/Width 6 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray'
        for separator in [b' ',b'\n',b'\r',b'\r\n',b'\t']:
            check('embedded-EI',b' EI q ',gray,separator=separator)
        check('stored-flate-markers',zlib.compress(b' EI q ',0),gray+' /Filter /FlateDecode',gray+' /F /Fl')
        data=literals(b'abcdef',1).hex().encode()+b'>'
        check('hex-lzw',data,gray+' /Filter [/ASCIIHexDecode /LZWDecode]',gray+' /F [/AHx /LZW]')
        fax_attrs='/Width 8 /Height 1 /BitsPerComponent 1 /ColorSpace /DeviceGray'
        fax_params=' /DecodeParms [null << /Columns 8 /Rows 1 /EndOfBlock false >>]'
        check('hex-fax',b'98>',fax_attrs+' /Filter [/ASCIIHexDecode /CCITTFaxDecode]'+fax_params,
              '/W 8 /H 1 /BPC 1 /CS /G /F [/AHx /CCF]'+fax_params)
        check('ignore-extra',b'abcdef',gray,gray+' /Length 999 /Subtype /Form /Private [(ID) <4549>]')
        check('escaped-keys',b'abcdef',gray,'/W#69dth 6 /H 1 /BPC 8 /CS /G')
        check('decode',b'abcdef',gray+' /Decode [1 0]','/W 6 /H 1 /BPC 8 /CS /G /D [1 0]')
        check('indexed',b'\x00\x01', '/Width 2 /Height 1 /BitsPerComponent 8 /ColorSpace [/Indexed /DeviceRGB 1 <ff000000ff00>]',
              '/W 2 /H 1 /BPC 8 /CS [/I /RGB 1 <ff000000ff00>]')
        check('stencil',b'\x50', '/Width 4 /Height 1 /ImageMask true', '/W 4 /H 1 /IM true',prefix=b'1 0 0 rg ')
        check('named-resource',b'\xff\x00\x00',
              '/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB',
              '/W 1 /H 1 /BPC 8 /CS /Custom',
              resources='/Resources << /ColorSpace << /Custom /DeviceRGB >> >>')
        check('named-indexed',b'\x00\x01',
              '/Width 2 /Height 1 /BitsPerComponent 8 /ColorSpace [/Indexed /DeviceRGB 1 <ff000000ff00>]',
              '/W 2 /H 1 /BPC 8 /CS /Custom',
              resources='/Resources << /ColorSpace << /Custom [/Indexed /DeviceRGB 1 <ff000000ff00>] >> >>')
        check('clip',b'abcdef',gray,prefix=b'0 0 9 12 re W n ')
        check('rotation',b'abcdef',gray,prefix=b'0 1 -1 0 18 0 cm ')
        check('binary-markers',b'endstream endobj BI EI',
              '/Width 22 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray')
        form=b'18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /G ID x EI'
        form_object=b'<< /Type /XObject /Subtype /Form /BBox [0 0 18 12] /Length '+str(len(form)).encode()+b' >> stream\n'+form+b'\nendstream'
        actual=render(inline_document(b'/Fm Do','/Resources << /XObject << /Fm 5 0 R >> >>',{5:form_object}))
        expected=render(fixture(b'x','/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray'))
        assert actual.returncode==expected.returncode==0 and actual.stdout==expected.stdout
        count+=1
        for spaces in ('/Custom /Custom','/Custom /Other /Other /Custom'):
            content=b'BI /W 1 /H 1 /BPC 8 /CS /Custom ID x EI'
            result=render(inline_document(content,'/Resources << /ColorSpace << '+spaces+' >> >>'))
            assert result.returncode==3,(spaces,result.stderr)
            count+=1
        # Content after EI must still execute; an ordinary raw payload containing
        # apparent operators must not change the graphics state.
        content=b'BI /W 6 /H 1 /BPC 8 /CS /G ID EI Q q EI 1 0 0 rg 0 0 18 12 re f'
        actual=render(inline_document(content))
        assert actual.returncode==0 and actual.stdout[12:]==bytes([255,0,0,255])*384
        count+=1
        for content in [b'ID',b'EI',b'1 BI /W 1 /H 1 /BPC 8 /CS /G ID x EI',
                        b'BI /W 1 /H 1 /BPC 8 /CS /G ID x',
                        b'BI /W 1 /H 1 /BPC 8 /CS /G ID x EIx',
                        b'BI /W 2 /H 1 /BPC 8 /CS /G ID x EI',
                        b'BI /W -1 /H 1 /BPC 8 /CS /G ID x EI',
                        b'BI /W 1 /H 1 /BPC 8 /CS /G /F /AHx ID gg> EI',
                        b'BI /W 1 /H 1 /BPC 8 /CS /G /F /Fl ID x EI',
                        b'BI /W 1 /H 1 /BPC 8 /CS /G /Unknown '+b'['*18+b'1'+b']'*18+b' ID x EI',
                        b'BI /W 1 /H 1 /BPC 8 /CS /G BI ID x EI']:
            result=render(inline_document(content))
            assert result.returncode==3,(content,result.returncode,result.stderr)
            count+=1
    print(f'PDF inline images: {count} checks passed; {references} optional independent comparisons')


if __name__=='__main__':
    main()
