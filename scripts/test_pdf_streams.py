#!/usr/bin/env python3
"""Check page dictionary scope and ASCIIHex content streams against raw pixels."""
from pathlib import Path
import base64
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document

probe = str(Path(sys.argv[1]).resolve())
content = b'1 0 0 rg 0 0 18 12 re f'


def pdf(data=content, filter_value=None, before='', page=None, catalog=None, pages=None, length_value=None, extra_objects=None):
    fields = f'{before} /Length {len(data) if length_value is None else length_value}'
    if filter_value is not None: fields += ' /Filter ' + filter_value
    extra = {4: b'<< ' + fields.encode() + b' >>\nstream\n' + data + b'\nendstream'}
    extra.update(extra_objects or {})
    if page is not None: extra[3] = page
    if catalog is not None: extra[1] = catalog
    if pages is not None: extra[2] = pages
    return document('/MediaBox [0 0 18 12]', extra=extra)


encoded = content.hex().encode() + b'>'
cases = [('raw', pdf(), False)]
for value in ('/ASCIIHexDecode', '/AHx', '[/ASCIIHexDecode]', '[ % comment\n /AHx ]'):
    cases.append((value, pdf(encoded, value), False))
cases.extend([
    ('hex-whitespace', pdf(b'\0\t\n\f\r '.join(encoded[i:i+2] for i in range(0, len(encoded), 2)), '/ASCIIHexDecode'), False),
    ('hex-odd-nibble', pdf(content.hex().encode() + b'2>', '/ASCIIHexDecode'), False),
    ('hex-uppercase', pdf(encoded.upper(), '/ASCIIHexDecode'), False),
    ('hex-empty', pdf(b'>', '/ASCIIHexDecode'), False),
    ('hex-invalid-char', pdf(b'xy>', '/ASCIIHexDecode'), True),
    ('hex-no-eod', pdf(encoded[:-1], '/ASCIIHexDecode'), True),
    ('hex-invalid-before-eod', pdf(b'0!>', '/ASCIIHexDecode'), True),
    ('unknown-filter', pdf(encoded, '/Imaginary'), True),
    ('filter-name-prefix', pdf(encoded, '/ASCIIHexDecodeExtra'), True),
    ('unimplemented-filter-chain', pdf(encoded, '[/ASCIIHexDecode /FlateDecode]'), True),
    ('nested-filter-decoy', pdf(before='/Nested << /Filter /FlateDecode >>'), False),
    ('string-filter-decoy', pdf(before='/Text (/FlateDecode)'), False),
    ('filter-key-prefix', pdf(before='/FilterExtra /FlateDecode'), False),
    ('flate-array', pdf(zlib.compress(content), '[/FlateDecode]'), False),
    ('flate-short-name', pdf(zlib.compress(content), '/Fl'), False),
    ('nested-page-type', pdf(page='<< /Resources << /Font << /F1 << /Type /Font >> >> >> /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 18 12] >>'), False),
    ('nested-contents-decoy', pdf(page='<< /Nested << /Contents 99 0 R >> /Contents 4 0 R /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] >>'), False),
    ('catalog-pages-decoy', pdf(catalog='<< /Nested << /Pages 99 0 R >> /PagesExtra 99 0 R /Type /Catalog /Pages 2 0 R >>'), False),
    ('nested-kids-decoy', pdf(pages='<< /Nested << /Kids [99 0 R] >> /Kids [3 0 R] /Count 1 /Type /Pages >>'), False),
    ('kids-missing-R', pdf(pages='<< /Kids [3 0] /Count 1 /Type /Pages >>'), True),
    ('kids-R-prefix', pdf(pages='<< /Kids [3 0 Really] /Count 1 /Type /Pages >>'), True),
])
for value in ('/ASCII85Decode', '/A85', '[/ASCII85Decode]'):
    cases.append(('ascii85-' + value, pdf(base64.a85encode(content) + b'~>', value), False))
for value in ('/RunLengthDecode', '/RL', '[/RunLengthDecode]'):
    cases.append(('runlength-' + value, pdf(bytes([len(content)-1]) + content + b'\x80', value), False))
cases.extend([
    ('ascii85-whitespace', pdf(b' \n'.join(bytes([c]) for c in base64.a85encode(content)) + b'~>', '/ASCII85Decode'), False),
    ('hex-flate-chain', pdf(zlib.compress(content).hex().encode() + b'>', '[/ASCIIHexDecode /FlateDecode]'), False),
    ('a85-flate-chain', pdf(base64.a85encode(zlib.compress(content)) + b'~>', '[/ASCII85Decode /FlateDecode]', before='/DecodeParms [null null]'), False),
    ('null-filter', pdf(filter_value='null'), False),
    ('empty-filter-array', pdf(filter_value='[]'), False),
    ('ascii85-overflow', pdf(b'uuuuu~>', '/ASCII85Decode'), True),
    ('ascii85-single-tail', pdf(b'!~>', '/ASCII85Decode'), True),
    ('ascii85-mid-z', pdf(b'!z~>', '/ASCII85Decode'), True),
    ('ascii85-no-eod', pdf(base64.a85encode(content), '/ASCII85Decode'), True),
    ('ascii85-bad-end', pdf(b'~ >', '/ASCII85Decode'), True),
    ('ascii85-bad-char', pdf(b'v~>', '/ASCII85Decode'), True),
    ('runlength-short-literal', pdf(b'\x04abc', '/RunLengthDecode'), True),
    ('runlength-short-repeat', pdf(b'\xff', '/RunLengthDecode'), True),
    ('runlength-no-eod', pdf(b'\x00a', '/RunLengthDecode'), True),
    ('filter-depth', pdf(filter_value='[' + '/AHx '*9 + ']'), True),
    ('filter-unclosed', pdf(filter_value='[/AHx'), True),
    ('params-short', pdf(zlib.compress(content).hex().encode() + b'>', '[/AHx /Fl]', before='/DecodeParms [null]'), True),
    ('params-long', pdf(zlib.compress(content), '/Fl', before='/DecodeParms [null null]'), True),
    ('predictor-not-ignored', pdf(zlib.compress(content), '/Fl', before='/DecodeParms << /Predictor 12 >>'), True),
])
for label, payload, filter_value in [('raw', content, None), ('flate', zlib.compress(content), '/FlateDecode'), ('hex', encoded, '/ASCIIHexDecode')]:
    cases.append(('indirect-length-' + label, pdf(payload, filter_value, length_value='5 0 R', extra_objects={5: str(len(payload))}), False))
    cases.append(('nested-length-' + label, pdf(payload, filter_value, before='/Nested << /Length 999 >>'), False))
    cases.append(('stream-word-in-string-' + label, pdf(payload, filter_value, before='/Note (stream)'), False))
for value in ('-1', '1.5', '2147483648', '999999999999999999999999', 'null'):
    cases.append(('bad-indirect-length-' + value, pdf(length_value='5 0 R', extra_objects={5: value}), True))
cases.append(('missing-indirect-length', pdf(length_value='99 0 R'), True))
cases.append(('short-stream-length', pdf(length_value='1'), True))
cases.append(('long-stream-length', pdf(length_value='999999'), True))
cases.append(('raw-empty', pdf(b''), False))
cases.append(('indirect-length-chain', pdf(length_value='5 0 R', extra_objects={5: '6 0 R', 6: str(len(content))}), False))
cases.append(('indirect-length-cycle', pdf(length_value='5 0 R', extra_objects={5: '6 0 R', 6: '5 0 R'}), True))
cases.append(('indirect-length-self-cycle', pdf(length_value='5 0 R', extra_objects={5: '5 0 R'}), True))
cases.append(('indirect-length-garbage', pdf(length_value='5 0 R', extra_objects={5: str(len(content))+' garbage'}), True))
cases.append(('nested-page-count-decoy', pdf(pages='<< /Nested << /Count 999 >> /Type /Pages /Kids [3 0 R] /Count 1 >>'), False))
cases.append(('indirect-page-count', pdf(pages='<< /Type /Pages /Kids [3 0 R] /Count 5 0 R >>', extra_objects={5: '1'}), False))

# Marked-content properties must not execute as drawing operators.
for name, properties in [
    ('basic', b'<< /MCID 0 >>'),
    ('nested', b'<< /Nested << /Array [1 (>> [) <4142> [2]] >> >>'),
    ('comment', b'<< /MCID 0 % >> ignored\n /Text (0 0 1 rg) >>'),
    ('escaped-string', b'<< /Text (a\\(b\\)c) >>'),
]:
    marked = b'/Span ' + properties + b' BDC ' + content + b' EMC'
    cases.append(('content-dictionary-' + name, pdf(marked), False))
for name, properties in [
    ('unterminated', b'<< /MCID 0'),
    ('mismatched-array', b'<< /A [1 >>'),
    ('invalid-hex', b'<< /A <xy> >>'),
    ('unterminated-string', b'<< /A (text >>'),
    ('nesting-budget', b'<< /A ' + b'['*64 + b']'*64 + b' >>'),
]:
    cases.append(('bad-content-dictionary-' + name, pdf(b'/Span ' + properties), True))

with tempfile.TemporaryDirectory(prefix='cupidimage-pdf-streams-') as directory:
    path = Path(directory) / 'input.pdf'
    baseline = None
    for name, data, reject in cases:
        path.write_bytes(data)
        result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=10)
        assert result.returncode == (3 if reject else 0), (name, result.stderr)
        if reject:
            assert result.stderr and not result.stdout
        elif name == 'raw':
            baseline = result.stdout
        elif name in ('hex-empty', 'raw-empty'):
            w, h, pages = struct.unpack('=III', result.stdout[:12])
            assert (w, h, pages) == (24, 16, 1)
            assert result.stdout[12:] == bytes([255]) * (w*h*4)
        else:
            assert result.stdout == baseline, name
print(f'PDF stream and dictionary regressions: {len(cases)} passed')
