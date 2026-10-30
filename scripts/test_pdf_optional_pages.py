#!/usr/bin/env python3
"""Check optional painting while retaining graphics/text state side effects."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_forms import form
from test_pdf_type3 import stream


CATALOG = '<< /Type /Catalog /Pages 2 0 R /OCProperties << /OCGs [90 0 R 91 0 R] /D << /OFF [91 0 R] >> >> >>'
GROUPS = {90: '<< /Type /OCG /Name (on) >>', 91: '<< /Type /OCG /Name (off) >>'}
PROPERTIES = '/Properties << /On 90 0 R /Off 91 0 R /Meta << /MCID 1 >> >>'


def page(content, extra=None, resources='', size=(18, 12)):
    objects = {1: CATALOG, **GROUPS, 10: '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'}
    objects.update(extra or {})
    return document(f'/MediaBox [0 0 {size[0]} {size[1]}] /Resources << ' + PROPERTIES +
                    ' /Font << /F 10 0 R >> ' + resources + ' >>', stream=content, extra=objects).replace(b'%PDF-1.4', b'%PDF-1.7', 1)


def compressed_page(updated=False):
    import zlib
    from test_pdf_xref import PDF
    pdf = PDF()
    pdf.obj(2, b'<< /Type /Pages /Count 1 /Kids [3 0 R] >>')
    pdf.obj(3, b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 18 12] /Resources << /Properties << /Off 91 0 R /P 20 0 R >> >> >>')
    pdf.obj(4, stream('/OC /Off BDC 1 0 0 rg 0 0 18 12 re f EMC /OC /P BDC 0 1 0 rg 0 0 18 12 re f EMC'))
    objects = {1: b'<< /Type /Catalog /Pages 2 0 R /OCProperties 31 0 R >>',
               31: b'<< /OCGs [90 0 R 91 0 R] /D 30 0 R >>',
               30: b'<< /OFF [91 0 R] /Intent 32 0 R >>', 32: b'[/View]',
               90: b'<< /Type /OCG /Name (same) >>', 91: b'<< /Type /OCG /Name (same) >>',
               20: b'<< /Type /OCMD /OCGs 91 0 R /P /AllOff >>'}
    header = b''; body = b''; entries = {}
    for index, (number, value) in enumerate(objects.items()):
        header += f'{number} {len(body)} '.encode()
        body += value + b' '
        entries[number] = (2, 55, index)
    pdf.obj(55, stream(zlib.compress(header + body), f'/Type /ObjStm /N {len(objects)} /First {len(header)} /Filter /FlateDecode'))
    previous = pdf.xref(100, {0: (0, 0, 65535), **pdf.objects, **entries})
    if updated:
        pdf.obj(30, b'<< /OFF [90 0 R] /ON [91 0 R] >>')
        pdf.table({30: pdf.objects[30]}, size=101, prev=previous)
    return bytes(pdf.data)


def main():
    probe, allocator = [str(Path(x).resolve()) for x in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupid-optional-pages-') as directory:
        path = Path(directory) / 'case.pdf'
        def render(data, aa=False, reject=False):
            nonlocal checks
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), '0',
                                     *(['--pdf-text-aa', '--pdf-graphics-aa'] if aa else [])],
                                    capture_output=True, timeout=30)
            assert (result.returncode != 0) == reject, (result.returncode, result.stderr)
            checks += 1
            if reject:
                return None
            w, h, n = struct.unpack('<III', result.stdout[:12])
            assert n == 1 and len(result.stdout) == 12 + w * h * 4
            return result.stdout[12:]
        def fault(data, aa=False, reject=False):
            nonlocal allocations
            path.write_bytes(data)
            result = subprocess.run([allocator, str(path), str(int(reject)), str(int(aa))],
                                    capture_output=True, text=True, check=True, timeout=120)
            allocations += int(result.stdout.split('checks')[0].split()[-1])
        fill = '0 0 18 12 re f'
        white = bytes((255,) * 4) * (24 * 16)
        for aa in (False, True):
            red = render(page('1 0 0 rg ' + fill), aa)
            assert render(page('/OC /Off BDC 1 0 0 rg ' + fill + ' EMC'), aa) == white
            assert render(page('/OC /Off BDC 1 0 0 rg EMC ' + fill), aa) == red
            for content in ('/OC /Off BDC /OC /On BDC ' + fill + ' EMC EMC',
                            '/OC /Off BDC /Tag BMC ' + fill + ' EMC EMC',
                            'q /OC /Off BDC Q ' + fill + ' EMC',
                            '/OC /Off BDC ' + fill + ' EMC S'):
                assert render(page(content), aa) == white
            for property_name in ('On', 'Meta', 'Missing'):
                assert render(page('/OC /' + property_name + ' BDC 1 0 0 rg ' + fill + ' EMC'), aa) == red
            assert render(page('/OC /Off BDC 0 0 9 6 re W n EMC 1 0 0 rg ' + fill), aa) == render(page('0 0 9 6 re W n 1 0 0 rg ' + fill), aa)
            assert render(page('/OC /Off BDC .5 0 0 .5 0 0 cm EMC 1 0 0 rg ' + fill), aa) == render(page('.5 0 0 .5 0 0 cm 1 0 0 rg ' + fill), aa)
            text = 'BT /F 9 Tf 2 3 Td '
            assert render(page(text + '/OC /Off BDC (A ) Tj EMC (B) Tj ET'), aa) == render(page(text + '3 Tr (A ) Tj 0 Tr (B) Tj ET'), aa)
            assert render(page(text + '(A) Tj /OC /Off BDC (B) Tj ET EMC'), aa) == render(page(text + '(A) Tj 3 Tr (B) Tj ET'), aa)
            for mode in range(8):
                actual = text + f'{mode} Tr /OC /Off BDC (A) Tj EMC ET 1 0 0 rg ' + fill
                expected = text + ('7' if mode >= 4 else '3') + ' Tr (A) Tj ET 1 0 0 rg ' + fill
                assert render(page(actual), aa) == render(page(expected), aa), (mode, aa)
            # An object's own OC entry and an enclosing marked span both gate Do.
            child = form('1 0 0 rg ' + fill, '/BBox [0 0 18 12] /OC 91 0 R')
            resources = '/XObject << /G 20 0 R >>'
            assert render(page('/G Do', {20: child}, resources), aa) == white
            assert render(page('/OC /Off BDC /G Do EMC', {20: child.replace(b'/OC 91 0 R', b'/OC 90 0 R')}, resources), aa) == white
            image = stream(bytes((255, 0, 0)), '/Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8 /OC 91 0 R')
            assert render(page('18 0 0 12 0 0 cm /G Do', {20: image}, resources), aa) == white
            inline = 'BI /W 1 /H 1 /CS /RGB /BPC 8 ID abc EI'
            assert render(page('/OC /Off BDC ' + inline + ' EMC 1 0 0 rg ' + fill), aa) == red
            # Resource construction inside a hidden span must remain usable later.
            mask = form('1 g 0 0 9 12 re f', '/BBox [0 0 18 12] /Group << /S /Transparency /CS /DeviceGray /I true >>')
            extra = {20: mask, 21: '<< /SMask << /S /Alpha /G 20 0 R >> >>'}
            resources = '/ExtGState << /M 21 0 R >>'
            assert render(page('/OC /Off BDC /M gs EMC 1 0 0 rg ' + fill, extra, resources), aa) == render(page('/M gs 1 0 0 rg ' + fill, extra, resources), aa)
            fault(page('/OC /Off BDC /M gs EMC 1 0 0 rg ' + fill, extra, resources), aa)
            tiled = stream('0 1 0 rg 0 0 9 12 re f', '/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 18 12] /XStep 18 /YStep 12 /Resources << >>')
            extra = {20: tiled}; resources = '/Pattern << /P 20 0 R >>'
            assert render(page('/OC /Off BDC /Pattern cs /P scn EMC ' + fill, extra, resources), aa) == render(page('/Pattern cs /P scn ' + fill, extra, resources), aa)
            fault(page('/OC /Off BDC /Pattern cs /P scn EMC ' + fill, extra, resources), aa)
            fault(page(text + '7 Tr /OC /Off BDC (A) Tj EMC ET ' + fill), aa)
        for updated in (False, True):
            expected = bytes((255, 0, 0, 255) if updated else (0, 255, 0, 255)) * (24 * 16)
            assert render(compressed_page(updated)) == expected
            fault(compressed_page(updated), aa=True)
        # Hidden painting consumes a path but does not need its flattened raster geometry.
        dense = '/OC /Off BDC ' + '0 0 18 12 re ' * 16385 + 'f EMC 0 0 18 12 re f'
        assert render(page(dense)) == bytes((0, 0, 0, 255)) * (24 * 16)
        child = form('/OC /On BDC 1 0 0 rg ' + fill + ' EMC',
                     '/BBox [0 0 18 12] /Resources << /Properties << /On 91 0 R >> >>')
        assert render(page('/G Do', {20: child}, '/XObject << /G 20 0 R >>')) == white
        glyph = '<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [.001 0 0 .001 0 0] /FirstChar 65 /LastChar 65 /Widths [1000] /Encoding << /Differences [65 /A] >> /CharProcs << /A 11 0 R >> /Resources << ' + PROPERTIES + ' >> >>'
        hidden_glyph = stream('1000 0 d0 /OC /Off BDC 1 0 0 rg 0 0 1000 1000 re f EMC')
        for aa in (False, True):
            assert render(page('BT /F 9 Tf 2 2 Td (A) Tj ET', {10: glyph, 11: hidden_glyph}), aa) == white
        from test_pdf_text_antialias import MAKERS
        extra = {1: CATALOG, **GROUPS,
                 3: '<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> ' + PROPERTIES + ' >> >>'}
        def embedded(maker, content, **options):
            return maker(content, extra=extra, **options).replace(b'%PDF-1.4', b'%PDF-1.7', 1)
        for kind, maker in MAKERS.items():
            for aa in (False, True):
                for mode in (0, 4, 7):
                    actual = f'BT /F 18 Tf 20 30 Td {mode} Tr /OC /Off BDC (A) Tj EMC ET 1 0 0 rg 0 0 120 90 re f'
                    control = 'BT /F 18 Tf 20 30 Td ' + ('7' if mode >= 4 else '3') + ' Tr (A) Tj ET 1 0 0 rg 0 0 120 90 re f'
                    assert render(embedded(maker, actual), aa) == render(embedded(maker, control), aa), (kind, mode, aa)
                fault(embedded(maker, actual), aa)
        for vertical in (False, True):
            actual = 'BT /F 18 Tf 60 60 Td /OC /Off BDC <0001> Tj EMC <0002> Tj ET'
            control = 'BT /F 18 Tf 60 60 Td 3 Tr <0001> Tj 0 Tr <0002> Tj ET'
            for aa in (False, True):
                assert render(embedded(MAKERS['tt'], actual, cid=True, vertical=vertical), aa) == render(embedded(MAKERS['tt'], control, cid=True, vertical=vertical), aa)
        bad = page('/OC << /Type /OCMD /VE [/Not] >> BDC 0 0 18 12 re f EMC')
        render(bad, reject=True); fault(bad, reject=True)
    print(f'PDF optional-content pages: {checks} checks; {allocations} allocation checks')


if __name__ == '__main__':
    main()
