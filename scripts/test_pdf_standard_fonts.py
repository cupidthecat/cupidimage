#!/usr/bin/env python3
"""Check standard-font advances without comparing substitute glyph outlines."""
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib
from generate_pdf_standard_metrics import SOURCE, metrics
from test_pdf_embedded_fonts import FONT
from test_pdf_pages import document
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm


def fixture(content, name, encoding='', attributes='', extra=None):
    objects = {
        5: f'<< /Type /Font /Subtype /Type1 /BaseFont /{name} {encoding} {attributes} >>'.encode(),
        6: b'<< /Type /Font /Subtype /TrueType /BaseFont /CupidImageTestShapes /FontDescriptor 8 0 R /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 65 /Widths [1000] >>',
        8: b'<< /Type /FontDescriptor /FontName /CupidImageTestShapes /Flags 32 /FontBBox [0 0 1000 1400] /ItalicAngle 0 /Ascent 1000 /Descent -200 /CapHeight 600 /StemV 80 /FontFile2 9 0 R >>',
        9: stream(zlib.compress(FONT), '/Filter /FlateDecode /Length1 '+str(len(FONT))),
    }
    objects.update(extra or {})
    return document('/MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R /Marker 6 0 R >> >>',
                    stream=content, extra=objects)


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = refs = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-standard-fonts-') as tmp:
        path = Path(tmp) / 'case.pdf'
        def same(name, codes, width, encoding='', attributes='', extra=None, reference=True):
            nonlocal checks, refs
            # Place the expected marker on the pixel grid to isolate metrics from edge rules.
            content = f'BT /F 30 Tf 3 Tr {60-width*.03:.8f} 30 Td <{codes.hex()}> Tj /Marker 30 Tf 0 Tr (A) Tj ET'
            expected = '60 30 12 18 re f'
            def render(text):
                path.write_bytes(fixture(text, name, encoding, attributes, extra))
                p = subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
                assert p.returncode == 0,(name,p.stderr)
                return p.stdout
            actual = render(content)
            assert actual == render(expected),(name,codes,'advance mismatch')
            checks += 1
            if '--reference' in sys.argv and reference:
                path.write_bytes(fixture(content,name,encoding,attributes,extra))
                prefix = Path(tmp)/'reference'
                p = subprocess.run(['pdftoppm','-r','96','-aa','no','-aaVector','no','-singlefile',str(path),str(prefix)],capture_output=True,timeout=20)
                assert p.returncode == 0,p.stderr
                w,h,rgb = read_ppm(prefix.with_suffix('.ppm'))
                actual_rgb = bytes(v for i,v in enumerate(actual[12:]) if i%4!=3)
                assert (w,h)==(160,120) and actual_rgb == rgb,(name,codes,'independent advance differs',sum(a!=b for a,b in zip(actual_rgb,rgb)))
                refs += 1
        for path_afm in sorted(SOURCE.glob('*.afm')):
            named, encoding = metrics(path_afm)
            codes = b'AWi '
            width = sum(named[encoding[c]] for c in codes)
            same(path_afm.stem,codes,width)
            same(path_afm.stem,b'AV',sum(named[encoding[c]] for c in b'AV'))
            same(path_afm.stem,b'\0',0)
        same('Helvetica',b'ABC',3*667,encoding='/Encoding << /Differences [65 /A /A /A] >>')
        same('Times-Roman',b'\x80',500,encoding='/Encoding /WinAnsiEncoding')
        same('Helvetica',b'\x80',667,encoding='/Encoding /MacRomanEncoding')
        same('Helvetica',b'A',500,attributes='/FirstChar 65 /LastChar 65 /Widths [500]')
        same('Helvetica',b'A',667,encoding='/Encoding 10 0 R',extra={10:b'<< /Differences 11 0 R >>',11:b'[65 /A]'} )
        same('Helvetica',b'A',667,encoding='/Encoding << /Differences [65 /#41] >>')
        same('Helvetica',b'AB',667+667,attributes='/ToUnicode 10 0 R',extra={10:stream(b'1 begincodespacerange <4142> <4142> endcodespacerange 1 beginbfchar <4142> <0043> endbfchar')})
        same('Helvetica',b'A',667,attributes='/ToUnicode 10 0 R',extra={10:stream(b'1 begincodespacerange <00> <FF> endcodespacerange 1 beginbfchar <41> <0057> endbfchar')})
        same('Helvetica',b'AB',500,attributes='/FirstChar 65 /LastChar 65 /Widths [500]')
        same('Helvetica',b'AB',800,attributes='/FirstChar 65 /LastChar 65 /Widths [500] /FontDescriptor 10 0 R',extra={10:b'<< /Type /FontDescriptor /FontName /Helvetica /Flags 32 /FontBBox [0 0 1000 1000] /ItalicAngle 0 /Ascent 800 /Descent -200 /CapHeight 700 /StemV 80 /MissingWidth 300 >>'})
        same('Helvetica',b'A',0,encoding='/Encoding << /Differences [65 /NoSuchGlyph] >>')
        same('Helvetica',b'A',278,encoding='/Encoding << /BaseEncoding /WinAnsiEncoding /Differences [65 /space] >>')
        # A named encoding must not inspect an unrelated dictionary later in the font.
        same('Helvetica',b'A',667,encoding='/Encoding /WinAnsiEncoding',attributes='/Extra << /Differences [65 /W] >>')
        same('Helvetica',b'A',667,encoding='/Encoding null')
    print(f'Standard PDF font metrics: {checks} checks passed; {refs} independent comparisons')

if __name__ == '__main__':
    main()
