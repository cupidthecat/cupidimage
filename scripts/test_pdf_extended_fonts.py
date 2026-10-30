#!/usr/bin/env python3
"""Verify extended standard-font glyphs against explicitly embedded font programs."""
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib
from generate_pdf_extended_fonts import FONTS, SOURCE
from generate_pdf_standard_metrics import metrics, SOURCE as AFMS
from test_pdf_standard_fonts import fixture
from test_pdf_type3 import stream


MISSING = """
abreve uhungarumlaut ecaron scommaaccent Uring aogonek uogonek Dcroat commaaccent Emacron
ccaron Ncommaaccent lacute Tcommaaccent Cacute Edotaccent scedilla lozenge Rcaron
Gcommaaccent Amacron rcaron Zdotaccent Omacron Racute Sacute dcaron Umacron uring Abreve
Tcaron partialdiff Nacute cacute nacute umacron Ncaron Gbreve Idotaccent summation racute
omacron Zacute greaterequal lcommaaccent tcaron eogonek Uogonek zacute iogonek amacron
sacute Delta ohungarumlaut Eogonek dcroat Scedilla lcaron Kcommaaccent Lacute edotaccent
Imacron Lcaron lessequal Uhungarumlaut emacron gbreve Scommaaccent Ohungarumlaut Ccaron
radical Dcaron rcommaaccent Rcommaaccent Lcommaaccent Aogonek zdotaccent Ecaron Iogonek
kcommaaccent ncaron tcommaaccent notequal gcommaaccent ncommaaccent imacron
""".split()


def main():
    probe, allocator = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-extended-fonts-') as tmp:
        path = Path(tmp) / 'case.pdf'
        for name, filename in sorted(FONTS.items()):
            named, _ = metrics(AFMS / (name + '.afm'))
            font = (SOURCE / filename).read_bytes()
            def render(content, glyph_name, embedded=False, attributes=''):
                nonlocal checks
                encoding = '/Encoding << /Differences [65 /' + glyph_name + '] >>'
                objects = None
                if embedded:
                    objects = {
                        5: ('<< /Type /Font /Subtype /TrueType /BaseFont /CupidExtendedTest '
                            '/FontDescriptor 10 0 R /FirstChar 65 /LastChar 65 /Widths [' + str(named[glyph_name]) + '] '
                            + encoding + ' >>').encode(),
                        10: b'<< /Type /FontDescriptor /FontName /CupidExtendedTest /Flags 32 /FontBBox [-2000 -2000 3000 3000] /ItalicAngle 0 /Ascent 2000 /Descent -1000 /CapHeight 1000 /StemV 80 /FontFile2 11 0 R >>',
                        11: stream(zlib.compress(font), '/Filter /FlateDecode'),
                    }
                data = fixture(content, name, encoding, attributes, objects)
                path.write_bytes(data)
                result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
                assert result.returncode == 0, (name, glyph_name, result.stderr)
                checks += 1
                return result.stdout, data
            # The complete known gap, not only a hand-picked accent.
            missing = list(MISSING)
            if name.startswith('Courier'):
                missing = [g for g in missing if g not in ('scedilla', 'Scedilla')]
            if name.startswith(('Courier', 'Times')):
                missing.remove('commaaccent')
            for glyph_name in missing:
                content = 'BT /F 30 Tf 20 30 Td (AA) Tj ET'
                actual, _ = render(content, glyph_name)
                expected, _ = render(content, glyph_name, True)
                assert actual == expected, (name, glyph_name, 'embedded outline mismatch')
                assert any(v != 255 for v in actual[12:]), (name, glyph_name, 'blank glyph')
            for mode in range(8):
                content = f'.75 w BT /F 30 Tf {mode} Tr 20 30 Td (A) Tj ET'
                if mode >= 4:
                    content += ' 1 0 0 rg 0 0 120 90 re f'
                actual, data = render(content, 'abreve')
                expected, _ = render(content, 'abreve', True)
                assert actual == expected, (name, mode)
                if mode in (0, 2, 7):
                    path.write_bytes(data)
                    result = subprocess.run([allocator, str(path), '0'], capture_output=True, text=True, timeout=120)
                    assert result.returncode == 0, (name, mode, result.stdout, result.stderr)
                    allocations += int(result.stdout.split('checks')[0].split()[-1])
            for matrix in ('0 1 -1 0 60 10', '1 .25 .3 1 10 20', '-1 0 0 1 100 30'):
                content = f'BT /F 20 Tf {matrix} Tm (A) Tj ET'
                assert render(content, 'abreve')[0] == render(content, 'abreve', True)[0], (name, matrix)
            # Alternate the compact and extended programs without changing advances.
            extra = {
                6: ('<< /Type /Font /Subtype /Type1 /BaseFont /' + name + ' >>').encode(),
                10: b'<< /Type /FontDescriptor /FontName /CupidExtendedTest /Flags 32 /FontBBox [-2000 -2000 3000 3000] /ItalicAngle 0 /Ascent 2000 /Descent -1000 /CapHeight 1000 /StemV 80 /FontFile2 11 0 R >>',
                11: stream(zlib.compress(font), '/Filter /FlateDecode'),
            }
            outputs = []
            for embedded in (False, True):
                content = 'BT /F 30 Tf 10 30 Td ' + ('(A) Tj /Marker 30 Tf (A) Tj /F 30 Tf (A) Tj' if embedded else '(ABA) Tj') + ' ET'
                if embedded:
                    extra[5] = ('<< /Type /Font /Subtype /TrueType /BaseFont /CupidExtendedTest /FontDescriptor 10 0 R /FirstChar 65 /LastChar 65 /Widths [' + str(named['abreve']) + '] /Encoding << /Differences [65 /abreve] >> >>').encode()
                path.write_bytes(fixture(content, name, '/Encoding << /Differences [65 /abreve /A] >>', extra=extra))
                result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
                assert result.returncode == 0, (name, result.stderr)
                outputs.append(result.stdout)
                checks += 1
            assert outputs[0] == outputs[1], (name, 'mixed programs')
    print(f'Extended standard fonts: {checks} rendering checks; {allocations} allocation checks')


if __name__ == '__main__':
    main()
