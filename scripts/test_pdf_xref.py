#!/usr/bin/env python3
"""Verify cross-reference precedence, generations and compressed-object addressing."""
from pathlib import Path
import base64
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_type3 import stream
from test_pdf_lzw import literals
from audit_pdf_corpus import read_ppm

RED = b'1 0 0 rg 0 0 18 12 re f'
BLUE = b'0 0 1 rg 0 0 18 12 re f'


class PDF:
    def __init__(self):
        self.data = bytearray(b'%PDF-1.7\n')
        self.objects = {}

    def obj(self, number, value, generation=0):
        offset = len(self.data)
        self.data += f'{number} {generation} obj\n'.encode() + value + b'\nendobj\n'
        self.objects[number] = (1, offset, generation)
        return offset

    def table(self, entries=None, size=None, prev=None, hybrid=None, root='1 0 R'):
        offset = len(self.data)
        entries = dict(entries or {0: (0, 0, 65535), **self.objects})
        size = size or max(entries) + 1
        self.data += b'xref\n'
        numbers = sorted(entries)
        while numbers:
            end = 1
            while end < len(numbers) and numbers[end] == numbers[end-1] + 1:
                end += 1
            group, numbers = numbers[:end], numbers[end:]
            self.data += f'{group[0]} {len(group)}\n'.encode()
            for number in group:
                kind, value, generation = entries[number]
                self.data += f'{value:010} {generation:05} {"n" if kind else "f"} \n'.encode()
        trailer = f'/Size {size} /Root {root}'
        if prev is not None:
            trailer += f' /Prev {prev}'
        if hybrid is not None:
            trailer += f' /XRefStm {hybrid}'
        self.data += f'trailer\n<< {trailer} >>\nstartxref\n{offset}\n%%EOF\n'.encode()
        return offset

    def xref(self, number, entries=None, size=None, prev=None, widths=(1, 4, 2), index=None,
             compressed=True, root='1 0 R', finish=True, predictor=False):
        offset = len(self.data)
        entries = dict(entries or {0: (0, 0, 65535), **self.objects})
        entries[number] = (1, offset, 0)
        size = size or max(entries) + 1
        index = index or [0, size]
        rows = bytearray()
        for start, count in zip(index[::2], index[1::2]):
            for obj in range(start, start + count):
                values = entries.get(obj, (0, 0, 0))
                for value, width in zip(values, widths):
                    if width:
                        rows += value.to_bytes(width, 'big')
        attrs = f'/Type /XRef /Size {size} /Root {root} /W [' + ' '.join(map(str, widths)) + ']'
        attrs += ' /Index [' + ' '.join(map(str, index)) + ']'
        if prev is not None:
            attrs += f' /Prev {prev}'
        if predictor:
            width = sum(widths)
            previous = bytes(width)
            encoded = bytearray()
            for pos in range(0, len(rows), width):
                row = rows[pos:pos+width]
                encoded += b'\x02' + bytes((a-b) % 256 for a,b in zip(row, previous))
                previous = row
            rows = encoded
            params = f'<< /Predictor 12 /Columns {width} >>'
            attrs += ' /DecodeParms ' + ('[null ' + params + ']' if compressed == 'chain' else params)
        if compressed in (True, 'flate', 'chain'):
            rows = zlib.compress(rows)
            if compressed == 'chain':
                rows = base64.a85encode(rows) + b'~>'
                attrs += ' /Filter [/ASCII85Decode /FlateDecode]'
            else:
                attrs += ' /Filter /FlateDecode'
        elif compressed == 'hex':
            rows = bytes(rows).hex().encode() + b'>'
            attrs += ' /Filter /ASCIIHexDecode'
        elif compressed == 'a85':
            rows = base64.a85encode(rows) + b'~>'
            attrs += ' /Filter /ASCII85Decode'
        elif compressed == 'lzw':
            rows = literals(rows, 1)
            attrs += ' /Filter /LZWDecode'
        elif compressed == 'rle':
            rows = b''.join(bytes([len(rows[i:i+128])-1])+rows[i:i+128] for i in range(0,len(rows),128))+b'\x80'
            attrs += ' /Filter /RunLengthDecode'
        self.obj(number, stream(bytes(rows), attrs))
        if finish:
            self.data += f'startxref\n{offset}\n%%EOF\n'.encode()
        return offset


def base(generation=0, small=False):
    pdf = PDF()
    pdf.obj(1, f'<< /Type /Catalog /Pages 2 {generation} R >>'.encode(), generation)
    pdf.obj(2, f'<< /Type /Pages /Kids [3 {generation} R] /Count 1 >>'.encode(), generation)
    pdf.obj(3, (f'<< /Type /Page /Parent 2 {generation} R /MediaBox [0 0 {9 if small else 18} 12] '
                f'/Contents 4 {generation} R /Resources << >> >>').encode(), generation)
    pdf.obj(4, stream(RED), generation)
    return pdf


def cases():
    pdf = base(); pdf.table(); yield 'classic', bytes(pdf.data), (255, 0, 0), False
    pdf = base(); pdf.xref(5); yield 'xref-stream', bytes(pdf.data), (255, 0, 0), False
    pdf = base(); previous = pdf.table(); pdf.obj(4, stream(BLUE))
    pdf.table({4: pdf.objects[4]}, size=5, prev=previous)
    yield 'incremental-replacement', bytes(pdf.data), (0, 0, 255), False
    pdf = base(); previous = pdf.table()
    pdf.table({0: (0, 4, 65535), 4: (0, 0, 1)}, size=5, prev=previous)
    yield 'free-content-is-null', bytes(pdf.data), (255, 255, 255), False
    pdf = base(); previous = pdf.table(); pdf.obj(4, stream(BLUE), 1)
    pdf.table({4: pdf.objects[4]}, size=5, prev=previous)
    yield 'stale-generation-is-null', bytes(pdf.data), (255, 255, 255), False
    pdf = base(); previous = pdf.table(); pdf.obj(4, stream(BLUE), 1)
    pdf.obj(3, b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 1 R /Resources << >> >>')
    pdf.table({3: pdf.objects[3], 4: pdf.objects[4]}, size=5, prev=previous)
    yield 'reused-generation', bytes(pdf.data), (0, 0, 255), False
    pdf = base(1); pdf.table(root='1 1 R')
    yield 'nonzero-generations', bytes(pdf.data), (255, 0, 0), False
    pdf = base(); previous = pdf.table(); pdf.obj(4, stream(BLUE))
    pdf.xref(5, {4: pdf.objects[4]}, prev=previous, widths=(0, 4, 0), index=[4, 2])
    yield 'implicit-stream-fields', bytes(pdf.data), (0, 0, 255), False
    pdf = base(); previous = pdf.table(); pdf.obj(4, stream(BLUE))
    pdf.xref(6, {4: pdf.objects[4]}, prev=previous, index=[4, 1, 6, 1])
    yield 'sparse-stream-update', bytes(pdf.data), (0, 0, 255), False
    pdf = base(); entries = {0: (0, 0, 65535), **pdf.objects}; entries[4] = (99, 0, 0)
    pdf.xref(5, entries)
    yield 'future-entry-is-null', bytes(pdf.data), (255, 255, 255), False
    for bad_index in (None, 0, 2):
        pdf = base(small=True); previous = pdf.table()
        first = b'null '
        page = b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Resources << >> >>'
        header = f'8 0 3 {len(first)} '.encode()
        pdf.obj(5, stream(zlib.compress(header + first + page),
                          f'/Type /ObjStm /N 2 /First {len(header)} /Filter /FlateDecode'))
        entries = {3: (2, 5, 1 if bad_index is None else bad_index), 5: pdf.objects[5], 8: (2, 5, 0)}
        pdf.xref(6, entries, size=9, prev=previous, index=[3, 1, 5, 2, 8, 1])
        yield 'compressed-replacement-' + str(bad_index), bytes(pdf.data), (255, 0, 0), bad_index is not None
    pdf = base(small=True); previous = pdf.table(); old_content = pdf.objects[4]
    pdf.obj(4, stream(BLUE))
    page = b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Resources << >> >>'
    pdf.obj(5, stream(b'3 0 ' + page, '/Type /ObjStm /N 1 /First 4'))
    hybrid = pdf.xref(6, {3: (2, 5, 0), 4: old_content}, size=7, index=[3, 2, 6, 1], finish=False)
    pdf.table({4: pdf.objects[4], 5: pdf.objects[5], 6: pdf.objects[6]}, size=7,
              prev=previous, hybrid=hybrid)
    yield 'hybrid-table-before-stream', bytes(pdf.data), (0, 0, 255), False
    pdf = base(); position = len(pdf.data); pdf.table(prev=position)
    yield 'previous-section-cycle', bytes(pdf.data), None, True


def extended_cases():
    for widths in ((1, 2, 1), (2, 8, 4), (12, 12, 12), (0, 2, 0), (0, 8, 0)):
        pdf = base(); previous = pdf.table(); pdf.obj(4, stream(BLUE))
        pdf.xref(5, {4: pdf.objects[4]}, prev=previous, widths=widths, index=[4, 2])
        yield 'field-widths-' + str(widths), bytes(pdf.data), (0, 0, 255), False
    for encoding in (False, 'flate', 'hex', 'a85', 'lzw', 'rle', 'chain'):
        pdf = base(); pdf.xref(5, compressed=encoding)
        yield 'stream-filter-' + str(encoding), bytes(pdf.data), (255, 0, 0), False
    for encoding in ('flate', 'chain'):
        pdf = base(); pdf.xref(5, compressed=encoding, predictor=True)
        yield 'xref-predictor-' + encoding, bytes(pdf.data), (255, 0, 0), False
    pdf = base(); pdf.xref(5, compressed=False)
    valid = bytes(pdf.data)
    for label, old, new in (
            ('short-W', b'/W [1 4 2]', b'/W [1 4]'),
            ('long-W', b'/W [1 4 2]', b'/W [1 4 2 0]'),
            ('negative-W', b'/W [1 4 2]', b'/W [1 -4 2]'),
            ('fractional-W', b'/W [1 4 2]', b'/W [1 4.5 2]'),
            ('indirect-W', b'/W [1 4 2]', b'/W 8 0 R'),
            ('odd-Index', b'/Index [0 6]', b'/Index [0 6 9]'),
            ('overlap-Index', b'/Index [0 6]', b'/Index [0 4 3 3]'),
            ('descending-Index', b'/Index [0 6]', b'/Index [3 3 0 3]'),
            ('negative-Index', b'/Index [0 6]', b'/Index [0 -6]'),
            ('indirect-Index', b'/Index [0 6]', b'/Index [0 8 0 R]'),
            ('zero-Size', b'/Size 6', b'/Size 0'),
            ('negative-Size', b'/Size 6', b'/Size -1'),
            ('indirect-Size', b'/Size 6', b'/Size 8 0 R'),
            ('missing-Root', b'/Root 1 0 R', b''),
            ('invalid-Type', b'/Type /XRef', b'/Type /Other'),
            ('short-Length', b'/Length 42', b'/Length 41'),
            ('long-Length', b'/Length 42', b'/Length 47'),
            ('indirect-Filter', b'/Index [0 6]', b'/Index [0 6] /Filter 8 0 R'),
            ('indirect-parameters', b'/Index [0 6]', b'/Index [0 6] /DecodeParms << /Columns 8 0 R >>'),
            ('external-stream', b'/Index [0 6]', b'/Index [0 6] /F (external.bin)')):
        assert old in valid, label
        yield label, valid.replace(old, new), None, True
    pdf = base(); pdf.xref(5, index=[0, 5])
    yield 'missing-self-entry', bytes(pdf.data), None, True
    pdf = base(); previous = pdf.table()
    pdf.table({4: pdf.objects[4]}, size=4, prev=previous)
    yield 'decreasing-Size', bytes(pdf.data), None, True
    pdf = base(); previous = pdf.table()
    pdf.table({4: (1, pdf.objects[1][1], 0)}, size=5, prev=previous)
    yield 'offset-header-mismatch', bytes(pdf.data), None, True
    pdf = base(); previous = pdf.table(); pdf.obj(4, stream(BLUE), 1)
    pdf.table({4: (1, pdf.objects[4][1], 0)}, size=5, prev=previous)
    yield 'generation-header-mismatch', bytes(pdf.data), None, True


def parameter_cases():
    for label, filter_value, params, extras, reject in (
        ('indirect-name', b'7 0 R', b'null', {7: b'/FlateDecode'}, False),
        ('escaped-name', b'/Fl#61teDecode', b'null', {}, False),
        ('indirect-array', b'7 0 R', b'8 0 R', {7: b'[9 0 R]', 8: b'[10 0 R]',
            9: b'/FlateDecode', 10: b'<< /Predictor 11 0 R /Columns 12 0 R >>',
            11: b'1', 12: b'1'}, False),
        ('indirect-dictionary', b'/FlateDecode', b'8 0 R', {8: b'<< /Predictor 9 0 R >>', 9: b'1'}, False),
        ('undefined-parameter', b'/FlateDecode', b'8 0 R', {}, False),
        ('null-controls', b'/FlateDecode', b'<< /Predictor null /Colors 90 0 R >>', {}, False),
        ('unknown-control', b'/FlateDecode', b'<< /Extension (ignored) >>', {}, False),
        ('filter-alias', b'7 0 R', b'null', {7: b'8 0 R', 8: b'/FlateDecode'}, False),
        ('filter-cycle', b'7 0 R', b'null', {7: b'8 0 R', 8: b'7 0 R'}, True),
        ('parameter-cycle', b'/FlateDecode', b'7 0 R', {7: b'8 0 R', 8: b'7 0 R'}, True),
        ('nested-cycle', b'/FlateDecode', b'<< /Columns 7 0 R >>', {7: b'7 0 R'}, True),
    ):
        for compressed_page in (False, True):
            pdf = base()
            for number, value in extras.items():
                pdf.obj(number, value)
            if compressed_page:
                page = b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Resources << >> >>'
                raw = b'3 0 ' + page
                attrs = '/Type /ObjStm /N 1 /First 4 '
                number = 5
            else:
                raw = RED
                attrs = ''
                number = 4
            attrs += '/Filter ' + filter_value.decode() + ' /DecodeParms ' + params.decode()
            pdf.obj(number, stream(zlib.compress(raw), attrs))
            if compressed_page:
                entries = {0: (0, 0, 65535), **pdf.objects, 3: (2, 5, 0)}
                pdf.xref(20, entries)
            else:
                pdf.table()
            yield ('object-stream-' if compressed_page else 'content-stream-') + label, bytes(pdf.data), (255, 0, 0), reject


def contents_cases():
    for label, contents, values, reject in (
        ('direct-array', b'[4 0 R 5 0 R]', {5: stream(BLUE)}, False),
        ('indirect-array', b'7 0 R', {7: b'[4 0 R 5 0 R]', 5: stream(BLUE)}, False),
        ('array-alias', b'7 0 R', {7: b'8 0 R', 8: b'[4 0 R 5 0 R]', 5: stream(BLUE)}, False),
        ('stream-alias', b'7 0 R', {7: b'5 0 R', 5: stream(BLUE)}, False),
        ('array-stream-alias', b'[4 0 R 7 0 R]', {7: b'5 0 R', 5: stream(BLUE)}, False),
        ('array-cycle', b'7 0 R', {7: b'8 0 R', 8: b'7 0 R'}, True),
        ('nested-array', b'[7 0 R]', {7: b'[4 0 R]'}, True),
    ):
        pdf = base()
        pdf.obj(3, b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Resources << >> /Contents ' + contents + b' >>')
        for number, value in values.items():
            pdf.obj(number, value)
        pdf.table()
        yield 'contents-' + label, bytes(pdf.data), (0, 0, 255), reject


def legacy_length_cases():
    for encoding in (False, 'flate', 'hex', 'a85', 'lzw', 'rle', 'chain'):
        for invalid in ('valid', 'short', 'cycle', 'new-revision'):
            pdf = base()
            pdf.obj(7, b'0000000000')
            offset = pdf.xref(8, compressed=encoding)
            prefix, tail = bytes(pdf.data[:offset]), bytes(pdf.data[offset:])
            match = re.search(rb'/Length (\d+)', tail)
            length = int(match[1])
            length_body = b'7 0 R' if invalid == 'cycle' else f'{length - (invalid == "short"):010}'.encode()
            prefix = prefix.replace(b'0000000000', length_body)
            # Preserve offsets when replacing the cycle's integer object body.
            if invalid == 'cycle':
                prefix = prefix.replace(b'7 0 R\nendobj', b'7 0 R     \nendobj')
            tail = tail[:match.start()] + b'/Length 7 0 R' + tail[match.end():]
            pdf.data = bytearray(prefix + tail)
            if invalid == 'new-revision':
                pdf.obj(7, b'0')
                pdf.table({7: pdf.objects[7]}, size=9, prev=offset)
            yield 'legacy-Length-' + str(encoding) + '-' + invalid, bytes(pdf.data), (255, 0, 0), invalid in ('short', 'cycle')


def object_stream_cases():
    page = b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents 4 0 R /Resources << >> >>'
    raw = b'3 0 ' + page
    for label, values, reject in (
        ('indirect-properties', {9: b'/ObjStm', 10: b'1', 11: b'4', 12: str(len(raw)).encode()}, False),
        ('property-aliases', {9: b'13 0 R', 13: b'/ObjStm', 10: b'1', 11: b'4', 12: str(len(raw)).encode()}, False),
        ('count-cycle', {9: b'/ObjStm', 10: b'10 0 R', 11: b'4', 12: str(len(raw)).encode()}, True),
        ('length-cycle', {9: b'/ObjStm', 10: b'1', 11: b'4', 12: b'12 0 R'}, True),
    ):
        pdf = base()
        for number, value in values.items():
            pdf.obj(number, value)
        pdf.obj(5, b'<< /Type 9 0 R /N 10 0 R /First 11 0 R /Length 12 0 R >>\nstream\n' + raw + b'\nendstream')
        pdf.xref(20, {0: (0, 0, 65535), **pdf.objects, 3: (2, 5, 0)})
        yield 'object-stream-' + label, bytes(pdf.data), (255, 0, 0), reject
    pdf = base(); pdf.xref(5)
    yield 'default-Index', bytes(pdf.data).replace(b' /Index [0 6]', b''), (255, 0, 0), False
    pdf = base(); pdf.xref(5)
    yield 'escaped-xref-filter', bytes(pdf.data).replace(b'/FlateDecode', b'/Fl#61teDecode'), (255, 0, 0), False


def main():
    probe = str(Path(sys.argv[1]).resolve()); checks = 0
    positional = [arg for arg in sys.argv[2:] if not arg.startswith('--')]
    oom = str(Path(positional[0]).resolve()) if positional else None
    allocation_checks = references = 0
    analytic_only = {
        'free-content-is-null', 'future-entry-is-null',
        'content-stream-filter-alias', 'object-stream-filter-alias',
        'contents-array-alias', 'contents-stream-alias', 'contents-array-stream-alias',
    }
    with tempfile.TemporaryDirectory(prefix='cupidimage-xref-') as tmp:
        for name, data, color, reject in (*cases(), *extended_cases(), *parameter_cases(), *contents_cases(), *legacy_length_cases(), *object_stream_cases()):
            path = Path(tmp) / (name + '.pdf'); path.write_bytes(data)
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert result.returncode == (3 if reject else 0), (name, result.stderr)
            if not reject:
                assert struct.unpack('=III', result.stdout[:12]) == (24, 16, 1), name
                assert result.stdout[12:] == bytes((*color, 255)) * (24 * 16), name
            # Null semantics and bounded reference-chain compatibility retain their
            # exact analytic expectations instead of adopting a recovery result.
            if '--reference' in sys.argv and not reject and name not in analytic_only:
                prefix = Path(tmp) / 'reference'
                ref = subprocess.run(['pdftoppm', '-r', '96', '-aa', 'no', '-aaVector', 'no',
                                      '-singlefile', str(path), str(prefix)], capture_output=True,
                                     timeout=30)
                assert ref.returncode == 0, (name, ref.stderr)
                w, h, rgb = read_ppm(prefix.with_suffix('.ppm'))
                assert (w, h) == (24, 16) and rgb == bytes(color) * (24 * 16), name
                references += 1
            if oom:
                faults = subprocess.run([oom, str(path), str(int(reject))], capture_output=True,
                                        text=True, timeout=60)
                assert faults.returncode == 0, (name, faults.stdout, faults.stderr)
                allocation_checks += int(faults.stdout.split(': ')[1].split()[0])
            checks += 1
    print(f'PDF cross-reference resolution: {checks} checks passed; {allocation_checks} allocation checks; {references} reference matches')


if __name__ == '__main__':
    main()
