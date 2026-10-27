#!/usr/bin/env python3
"""Compare built-in standard substitutes with the same outlines embedded in PDF."""
from pathlib import Path
import subprocess
import sys
import tempfile
from generate_pdf_standard_metrics import SOURCE, metrics
from generate_pdf_standard_outlines import FONTS, font_bytes
from test_pdf_standard_fonts import fixture
from test_pdf_type3 import stream


def main():
    probe=str(Path(sys.argv[1]).resolve())
    checks=0
    with tempfile.TemporaryDirectory(prefix='cupidimage-standard-outlines-') as tmp:
        path=Path(tmp)/'case.pdf'
        for name,stem in sorted(FONTS.items()):
            named,encoding=metrics(SOURCE/(name+'.afm'))
            widths=' '.join(str(named.get(encoding.get(c),0)) for c in range(256))
            differences=' '.join('/'+encoding.get(c,'.notdef') for c in range(256))
            embedded={5:('<< /Type /Font /Subtype /Type1 /BaseFont /CupidStandardTest /FontDescriptor 10 0 R /FirstChar 0 /LastChar 255 /Widths ['+widths+'] /Encoding << /Differences [0 '+differences+'] >> >>').encode(),
                      10:b'<< /Type /FontDescriptor /FontName /CupidStandardTest /Flags 32 /FontBBox [-1000 -1000 2000 2000] /ItalicAngle 0 /Ascent 1000 /Descent -300 /CapHeight 700 /StemV 80 /FontFile3 11 0 R >>',
                      11:stream(font_bytes(stem),'/Subtype /Type1C')}
            def render(content,extra=None,encoding_value=''):
                path.write_bytes(fixture(content,name,encoding_value,extra=extra))
                result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
                assert result.returncode==0,(name,result.stderr)
                return result.stdout
            for mode in range(8):
                content=f'0.75 w BT /F 22 Tf {mode} Tr 20 30 Td (AWi) Tj ET'
                if mode>=4:content+=' 1 0 0 rg 0 0 120 90 re f'
                actual=render(content)
                assert actual==render(content,embedded),(name,mode,'embedded substitute differs')
                assert mode==3 or any(c!=255 for c in actual[12:]),(name,mode,'unexpected empty rendering')
                checks+=1
            for matrix in ('0 1 -1 0 60 10','1 .25 .3 1 10 20','-1 0 0 1 100 30'):
                content=f'BT /F 20 Tf {matrix} Tm (AWi) Tj ET'
                assert render(content)==render(content,embedded),(name,matrix)
                checks+=1
            # ToUnicode maps extraction text, not the outline to paint.
            content='BT /F 22 Tf 20 30 Td (A) Tj ET'
            altered={12:stream(b'1 begincodespacerange <00> <FF> endcodespacerange 1 beginbfchar <41> <0057> endbfchar'),
                     5:f'<< /Type /Font /Subtype /Type1 /BaseFont /{name} /ToUnicode 12 0 R >>'.encode()}
            assert render(content)==render(content,altered),(name,'ToUnicode replaced outline')
            checks+=1
            name_a=encoding[65]
            assert render(content)==render('BT /F 22 Tf 20 30 Td (B) Tj ET',encoding_value=f'/Encoding << /Differences [66 /{name_a}] >>'),(name,'Differences')
            checks+=1
        name='Helvetica'
        content='BT /F 22 Tf 20 30 Td (AWi) Tj ET'
        path.write_bytes(fixture(content,name))
        baseline=subprocess.check_output([probe,str(path),'0'])
        cases=[('/Identity-H',{}),('999 0 R',{}),
               ('12 0 R',{12:stream(b'broken','/Filter /FlateDecode')}),
               ('12 0 R',{12:stream(b'1 beginbfchar <41> <0057> endbfchar 1 beginbfrange <0000> <FFFFFFFF> <0041> endbfrange')})]
        for value,extra in cases:
            path.write_bytes(fixture(content,name,attributes='/ToUnicode '+value,extra=extra))
            result=subprocess.run([probe,str(path),'0'],capture_output=True,timeout=20)
            assert result.returncode==0 and result.stdout==baseline,('invalid ToUnicode replaced outlines',value,result.stderr)
            checks+=1
    print(f'Standard substitute rendering: {checks} built-in/embedded and glyph-selection checks passed')

if __name__=='__main__':main()
