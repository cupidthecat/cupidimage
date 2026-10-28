#!/usr/bin/env python3
"""Check deterministic nonembedded simple-font substitutes and PDF text metrics."""
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib
from generate_pdf_extended_fonts import FONTS, SOURCE
from test_pdf_pages import document
from test_pdf_type3 import stream
from test_pdf_embedded_fonts import FONT

ALIASES = {
    'Helvetica': ('Arial', 'ArialMT', 'LiberationSans'),
    'Helvetica-Bold': ('Arial-Bold', 'Arial-BoldMT', 'Arial,Bold', 'LiberationSans-Bold'),
    'Helvetica-Oblique': ('Arial-Italic', 'Arial-ItalicMT', 'Arial,Italic', 'LiberationSans-Italic'),
    'Helvetica-BoldOblique': ('Arial-BoldItalic', 'Arial-BoldItalicMT', 'Arial,BoldItalic', 'LiberationSans-BoldItalic'),
    'Times-Roman': ('TimesNewRoman', 'TimesNewRomanPSMT', 'LiberationSerif'),
    'Times-Bold': ('TimesNewRoman-Bold', 'TimesNewRomanPS-BoldMT', 'TimesNewRoman,Bold', 'LiberationSerif-Bold'),
    'Times-Italic': ('TimesNewRoman-Italic', 'TimesNewRomanPS-ItalicMT', 'TimesNewRoman,Italic', 'LiberationSerif-Italic'),
    'Times-BoldItalic': ('TimesNewRoman-BoldItalic', 'TimesNewRomanPS-BoldItalicMT', 'TimesNewRoman,BoldItalic', 'LiberationSerif-BoldItalic'),
    'Courier': ('CourierNew', 'CourierNewPSMT', 'LiberationMono'),
    'Courier-Bold': ('CourierNew-Bold', 'CourierNewPS-BoldMT', 'CourierNew,Bold', 'LiberationMono-Bold'),
    'Courier-Oblique': ('CourierNew-Italic', 'CourierNewPS-ItalicMT', 'CourierNew,Italic', 'LiberationMono-Italic'),
    'Courier-BoldOblique': ('CourierNew-BoldItalic', 'CourierNewPS-BoldItalicMT', 'CourierNew,BoldItalic', 'LiberationMono-BoldItalic'),
}


def fixture(name='Arial', subtype='TrueType', encoding='/WinAnsiEncoding', content=None,
            embedded=None, descriptor='', attributes='', flags=32, absent_descriptor=False, extra=None):
    encoding = '/Encoding ' + encoding if encoding else ''
    font = f'<< /Type /Font /Subtype /{subtype} /BaseFont /{name} {encoding} /FirstChar 0 /LastChar 255 /Widths [' + '700 '*256 + '] ' + attributes
    if not absent_descriptor:
        font += ' /FontDescriptor 6 0 R'
    font += ' >>'
    desc = f'<< /Type /FontDescriptor /FontName /{name} /Flags {flags} /FontBBox [-2000 -2000 3000 3000] /ItalicAngle 0 /Ascent 2000 /Descent -1000 /CapHeight 1000 /StemV 80 {descriptor}'
    objects = {5: font}
    if embedded is not None:
        desc += ' /FontFile2 7 0 R'
        objects[7] = stream(zlib.compress(embedded), '/Filter /FlateDecode')
    objects[6] = desc + ' >>'
    objects.update(extra or {})
    return document('/MediaBox [0 0 180 90] /Resources << /Font << /F 5 0 R >> >>',
                    stream=content or 'BT /F 22 Tf 10 30 Td <41422080e9> Tj ET', extra=objects)


def main():
    probe, allocator = [str(Path(a).resolve()) for a in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-substitution-') as tmp:
        path = Path(tmp) / 'case.pdf'
        def render(data, reject=False):
            nonlocal checks
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert (result.returncode != 0) == reject, result.stderr
            checks += 1
            return result.stdout
        def fault(data):
            nonlocal allocations
            path.write_bytes(data)
            result = subprocess.run([allocator, str(path), '0'], capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, (result.stdout, result.stderr)
            allocations += int(result.stdout.split('checks')[0].split()[-1])
        for face, aliases in ALIASES.items():
            data = (SOURCE / FONTS[face]).read_bytes()
            expected = render(fixture(embedded=data))
            for name in aliases:
                for subtype in ('Type1', 'TrueType'):
                    assert render(fixture(name, subtype)) == expected, (name, subtype)
            for encoding in ('', 'null', '/StandardEncoding', '/MacRomanEncoding',
                             '<< /Differences [65 /abreve 66 /Euro] >>',
                             '<< /BaseEncoding /WinAnsiEncoding /Differences [65 /uni0042] >>'):
                for subtype in ('Type1', 'TrueType'):
                    expected_encoding = '/StandardEncoding' if subtype == 'Type1' and encoding in ('', 'null') else encoding
                    actual = fixture(aliases[0], subtype, encoding)
                    expected = fixture(embedded=data, encoding=expected_encoding)
                    assert render(actual) == render(expected), (face, subtype, encoding)
            for mode in range(8):
                content = f'.75 w BT /F 22 Tf {mode} Tr 80 Tz 1 .25 .1 1 20 30 Tm (ABA) Tj ET'
                if mode >= 4:
                    content += ' 1 0 0 rg 0 0 180 90 re f'
                actual = fixture(aliases[0], content=content)
                assert render(actual) == render(fixture(embedded=data, content=content)), (face, mode)
                if mode in (0, 2, 7):
                    fault(actual)
        data = (SOURCE / FONTS['Helvetica']).read_bytes()
        for encoding in ('42', '[]', '(broken)', '/UnknownEncoding'):
            render(fixture(encoding=encoding), True)
            render(fixture(encoding=encoding, embedded=data), True)
        for descriptor in ('/FontFile3 8 0 R', '/FontFile2 8 0 R'):
            render(fixture(descriptor=descriptor, extra={8: stream('broken', '/Subtype /Unsupported')}), True)
        for missing in (False, True):
            actual = fixture(absent_descriptor=missing)
            assert render(actual) == render(fixture(embedded=data))
        assert render(fixture(embedded=FONT)) == render(fixture(name='CupidEmbedded', embedded=FONT))
        unknown = render(fixture(name='CupidUnknown'))
        for name in ('ArialNarrow', 'arial', 'ABCDEF+Arial'):
            assert render(fixture(name=name)) == unknown, name
        render(fixture().replace(b'/BaseFont /Arial', b'/BaseFont 42'), True)
        for encoding in ('/WinAnsiEncoding', '<< /Differences [65 /abreve] >>', '42', ''):
            assert render(fixture(flags=4, encoding=encoding)) == render(fixture(flags=4, encoding=encoding, embedded=data))
        assert render(fixture(subtype='Type1', flags=4)) == render(fixture(subtype='Type1', flags=32))
        original = fixture()
        mapped = fixture(attributes='/ToUnicode 8 0 R', extra={8: stream('1 beginbfchar <41> <005A> endbfchar')})
        assert render(original) == render(mapped)
    print(f'PDF simple-font substitutes: {checks} checks; {allocations} allocation checks')


if __name__ == '__main__':
    main()
