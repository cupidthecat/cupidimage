#!/usr/bin/env python3
"""Check optional font values against omitted-entry and explicit-width equivalents."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_standard_fonts import fixture as standard
from test_pdf_pages import document
from test_pdf_forms import form
from test_pdf_type3 import stream
from test_pdf_embedded_fonts import fixture as embedded
from test_pdf_cff_render import fixture as compact

CONTENT = 'BT /F 30 Tf 20 30 Td (AA) Tj ET'
DESCRIPTOR = ('<< /Type /FontDescriptor /FontName /CupidImageTestShapes /Flags 32 '
              '/FontBBox [0 0 1000 1400] /ItalicAngle 0 /Ascent 1000 /Descent -200 '
              '/CapHeight 600 /StemV 80 /FontFile2 9 0 R %s >>')


def cases():
    for value in ('null', '90 0 R', '91 0 R', '0 0 R', '90 1 R'):
        extra = {90: b'null'}
        for key in ('ToUnicode', 'FontDescriptor', 'Widths'):
            yield 'standard-' + key + '-' + value, standard(CONTENT, 'Helvetica'), standard(
                CONTENT, 'Helvetica', attributes=f'/{key} {value}', extra=extra)
        yield 'standard-Encoding-' + value, standard(CONTENT, 'Helvetica'), standard(
            CONTENT, 'Helvetica', encoding='/Encoding ' + value, extra=extra)
        yield 'embedded-Encoding-' + value, embedded(CONTENT, encoding=''), embedded(
            CONTENT, encoding=value, extra=extra)
        yield 'embedded-Widths-' + value, embedded(CONTENT, metrics=''), embedded(
            CONTENT, metrics='/Widths ' + value, extra=extra)
        yield 'embedded-MissingWidth-' + value, embedded(CONTENT), embedded(
            CONTENT, extra={**extra, 8: (DESCRIPTOR % ('/MissingWidth ' + value)).encode()})
        descriptor = (DESCRIPTOR % ('/FontFile2 ' + value)).replace('/FontFile2 9 0 R', '/FontFile3 9 0 R')
        yield 'null-program-before-CFF-' + value, compact(CONTENT), compact(
            CONTENT, extra={**extra, 8: descriptor.encode()})
        for key in ('BaseEncoding', 'Differences'):
            for label, make in (('standard', lambda e, x: standard(CONTENT, 'Helvetica', encoding='/Encoding ' + e, extra=x)),
                                ('embedded', lambda e, x: embedded(CONTENT, encoding=e, extra=x))):
                yield label + '-' + key + '-' + value, make('<< >>', {}), make(f'<< /{key} {value} >>', extra)
    yield 'indirect-width', embedded(CONTENT, metrics='/FirstChar 65 /LastChar 65 /Widths [500]'), embedded(
        CONTENT, metrics='/FirstChar 65 /LastChar 65 /Widths [90 0 R]', extra={90: b'500'})
    yield 'indirect-MissingWidth', embedded(CONTENT, metrics='', extra={8: (DESCRIPTOR % '/MissingWidth 500').encode()}), embedded(
        CONTENT, metrics='', extra={8: (DESCRIPTOR % '/MissingWidth 90 0 R').encode(), 90: b'500'})

    for vertical in (False, True):
        content = 'BT /F 18 Tf 50 60 Td <00010001> Tj ET'
        for value in ('null', '90 0 R', '91 0 R', '0 0 R', '90 1 R'):
            for key in ('DW', 'DW2', 'W', 'W2'):
                yield f'CID-{vertical}-{key}-{value}', embedded(content, cid=True, vertical=vertical), embedded(
                    content, cid=True, vertical=vertical, metrics=f'/{key} {value}', extra={90: b'null'})
            descendant = ('<< /Type /Font /Subtype /CIDFontType2 /BaseFont /CupidImageTestShapes '
                          '/FontDescriptor 8 0 R /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> '
                          '/CIDToGIDMap ' + value + ' >>').encode()
            yield f'CID-{vertical}-CIDToGIDMap-{value}', embedded(content, cid=True, vertical=vertical), embedded(
                content, cid=True, vertical=vertical, extra={6: descendant, 90: b'null'})
    cmap = b'1 begincodespacerange <0000> <ffff> endcodespacerange 1 begincidchar <0001> 1 endcidchar'
    cid_font = b'<< /Type /Font /Subtype /Type0 /BaseFont /CupidImageTestShapes /Encoding 20 0 R /DescendantFonts [6 0 R] >>'
    content = 'BT /F 18 Tf 50 60 Td <00010001> Tj ET'
    baseline = embedded(content, cid=True, extra={5: cid_font, 20: stream(cmap, '/Type /CMap /CMapName /Test')})
    for value in ('null', '90 0 R', '91 0 R', '0 0 R', '90 1 R'):
        for key in ('WMode', 'UseCMap'):
            yield 'CMap-' + key + '-' + value, baseline, embedded(content, cid=True,
                extra={5: cid_font, 20: stream(cmap, '/Type /CMap /CMapName /Test /' + key + ' ' + value), 90: b'null'})
    font = b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'
    resource = '/Resources << /Font << /F 5 0 R >> >>'
    page = '/MediaBox [0 0 120 90]'
    for value in ('null', '90 0 R', '91 0 R', '0 0 R', '90 1 R'):
        objects = {5: font, 90: b'null'}
        baseline = document(page, resource, CONTENT, objects)
        yield 'inherited-Resources-' + value, baseline, document(
            page + '/Resources ' + value, resource, CONTENT, objects)
        yield 'optional-resource-category-' + value, baseline, document(
            page + '/Resources << /Font << /F 5 0 R >> /ExtGState ' + value + ' >>',
            stream=CONTENT, extra=objects)
        yield 'unused-null-font-' + value, baseline, document(
            page + '/Resources << /Font << /F 5 0 R /Unused ' + value + ' >> >>',
            stream=CONTENT, extra=objects)
        yield 'unused-null-state-' + value, baseline, document(
            page + '/Resources << /Font << /F 5 0 R >> /ExtGState << /Unused ' + value + ' >> >>',
            stream=CONTENT, extra=objects)

        yield 'form-inherited-Resources-' + value, baseline, document(
            page + '/Resources << /Font << /F 5 0 R >> /XObject << /Form 6 0 R >> >>',
            stream='/Form Do', extra={**objects, 6: form(CONTENT, '/BBox [0 0 120 90] /Resources ' + value)})


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = allocation_checks = 0
    oom = str(Path(sys.argv[2]).resolve()) if len(sys.argv) > 2 else None
    with tempfile.TemporaryDirectory(prefix='cupidimage-font-defaults-') as tmp:
        path = Path(tmp) / 'case.pdf'
        def render(data):
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert result.returncode == 0, result.stderr
            return result.stdout
        for name, baseline, changed in cases():
            expected = render(baseline)
            actual = render(changed)
            assert actual == expected, (name, sum(a != b for a, b in zip(actual, expected)))
            assert any(c != 255 for c in expected[12:]), (name, 'empty baseline')
            if oom:
                faults = subprocess.run([oom, str(path), '0'], capture_output=True, text=True, timeout=60)
                assert faults.returncode == 0, (name, faults.stdout, faults.stderr)
                allocation_checks += int(faults.stdout.split(': ')[1].split()[0])
            checks += 1
        for label, value, objects in (
            ('cyclic-width', '90 0 R', {90: b'90 0 R'}),
            ('null-width', 'null', {}),
            ('undefined-width', '91 0 R', {}),
            ('nonnumeric-width', '/Width', {}),
        ):
            path.write_bytes(embedded(CONTENT, metrics=f'/FirstChar 65 /LastChar 65 /Widths [{value}]', extra=objects))
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert result.returncode == 3, (label, result.stderr)
            checks += 1
    print(f'PDF font defaults: {checks} checks passed; {allocation_checks} allocation checks')


if __name__ == '__main__':
    main()
