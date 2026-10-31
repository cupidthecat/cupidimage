#!/usr/bin/env python3
"""Check recovered CID outlines, rendering modes and owned font lifetimes."""
import itertools
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_subset_fonts import tt_pair, repack
from test_pdf_embedded_fonts import fixture as tt_pdf
from test_pdf_cff_render import fixture as cff_pdf, BOX, CURVE
from test_pdf_cff import fixture as cff_font
from test_pdf_type3 import stream
from test_pdf_forms import form
from test_pdf_page_hinting import FONT as HINT_FONT, font_descriptor
from test_pdf_truetype import tables


def main():
    probe, allocator = [str(Path(x).resolve()) for x in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupid-subset-pages-') as directory:
        path = Path(directory) / 'case.pdf'
        def render(data, hinted=False, reject=False):
            nonlocal checks
            path.write_bytes(data)
            flags = ['--pdf-text-aa', '--pdf-graphics-aa', '--pdf-tt-hinting',
                     '--pdf-cff-hinting', '--pdf-type1-hinting'] if hinted else []
            result = subprocess.run([probe, str(path), '0', *flags], capture_output=True, timeout=30)
            assert result.returncode == (3 if reject else 0), (result.returncode, result.stderr)
            checks += 1
            if reject:
                return None
            w, h, n = struct.unpack('<III', result.stdout[:12])
            assert n == 1 and len(result.stdout) == 12 + w * h * 4
            return result.stdout[12:]
        def fault(data, hinted=False, reject=False):
            nonlocal allocations
            path.write_bytes(data)
            result = subprocess.run([allocator, str(path), str(int(reject)),
                                     *(['1'] * 5 if hinted else [])],
                                    capture_output=True, text=True, check=True, timeout=120)
            allocations += int(result.stdout.split('checks')[0].split()[-1])
        pairs = [tt_pair(True, False), tt_pair(False, True), tt_pair(True, True),
                 tt_pair(True, False, HINT_FONT)]
        for index, (control, broken, count) in enumerate(pairs):
            codes = '<00010002>' if index == 3 else '<0001000200040005>'
            for mode, hinted, vertical in itertools.product(range(8), (False, True), (False, True)):
                content = f'1 0 0 rg 0 0 1 RG 1 w BT /F 18 Tf 25 55 Td {mode} Tr {codes} Tj ET'
                if mode >= 4:
                    content += ' 0 .5 1 rg 0 0 120 90 re f'
                actual = tt_pdf(content, cid=True, vertical=vertical, font_data=broken)
                expected = tt_pdf(content, cid=True, vertical=vertical, font_data=control)
                assert render(actual, hinted) == render(expected, hinted), (index, mode, hinted, vertical)
            content = 'BT /F 18 Tf 20 30 Td 2 Tr ' + codes + ' Tj ET'
            fault(tt_pdf(content, cid=True, font_data=broken), True)
            render(tt_pdf('BT /F 18 Tf 20 30 Td (A) Tj ET', font_data=broken), reject=True)
            layout = tables(broken); start, length = layout[b'loca'][1:]
            offsets = bytearray(broken[start:start+length]);struct.pack_into('>I', offsets, 4, 0xffffffff)
            invalid = repack(broken, {b'loca': bytes(offsets)})
            data = tt_pdf(content, cid=True, font_data=invalid)
            render(data, True, True);fault(data, True, True)
        control, broken, _ = pairs[2]
        # CIDToGIDMap stays authoritative, independently of Unicode metadata.
        descendant = '<< /Type /Font /Subtype /CIDFontType2 /BaseFont /CupidImageTestShapes /FontDescriptor 8 0 R /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap 12 0 R /DW 1000 >>'
        extra = {6: descendant, 12: stream(bytes((0, 0, 0, 5, 0, 1)))}
        content = 'BT /F 18 Tf 20 30 Td <00010002> Tj ET'
        for hinted in (False, True):
            assert render(tt_pdf(content, cid=True, font_data=broken, extra=extra), hinted) == render(tt_pdf(content, cid=True, font_data=control, extra=extra), hinted)
        # A local font survives its Form resource table through the prepared-font cache.
        def local(data):
            child = form('BT /Local 18 Tf 20 30 Td <0005> Tj ET',
                         '/BBox [0 0 120 90] /Resources << /Font << /Local 30 0 R >> >>')
            objects = {20: child,
                       30: '<< /Type /Font /Subtype /Type0 /BaseFont /CupidImageTestShapes /Encoding /Identity-H /DescendantFonts [31 0 R] >>',
                       31: descendant.replace('8 0 R', '32 0 R').replace('12 0 R', '/Identity'),
                       32: font_descriptor(data, 'CupidImageTestShapes', 33),
                       33: stream(zlib.compress(data), '/Filter /FlateDecode /Length1 ' + str(len(data)))}
            return tt_pdf('/G Do BT /F 18 Tf 60 30 Td <0001> Tj ET', cid=True, font_data=control,
                          extra={**objects, 3: '<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> /XObject << /G 20 0 R >> >> >>'})
        assert render(local(broken), True) == render(local(control), True)
        fault(local(broken), True)
        programs = [b'\x0e', b'\x0e', BOX, CURVE]
        good = cff_font(cid=True, programs=programs, fdselect=b'\3\0\1\0\0\0\0\4')
        bad = cff_font(cid=True, programs=programs, fdselect=b'\3\0\1\0\1\0\0\4')
        for mode, hinted, vertical in itertools.product(range(8), (False, True), (False, True)):
            content = f'1 0 0 rg 0 0 1 RG BT /F 18 Tf 25 55 Td {mode} Tr <00220187> Tj ET'
            if mode >= 4:
                content += ' 0 .5 1 rg 0 0 120 90 re f'
            assert render(cff_pdf(content, cid=True, vertical=vertical, font=bad), hinted) == render(cff_pdf(content, cid=True, vertical=vertical, font=good), hinted)
        fault(cff_pdf(content, cid=True, font=bad), True)
        ambiguous = cff_font(cid=True, programs=programs, private_count=2, fdselect=b'\3\0\1\0\1\0\0\4')
        data = cff_pdf(content, cid=True, font=ambiguous)
        render(data, True, True);fault(data, True, True)
    print(f'PDF subset-font pages: {checks} checks; {allocations} allocation checks')


if __name__ == '__main__':
    main()
