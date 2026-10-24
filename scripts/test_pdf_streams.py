#!/usr/bin/env python3
"""Check page dictionary scope and ASCIIHex content streams against raw pixels."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from test_pdf_pages import document

probe = str(Path(sys.argv[1]).resolve())
content = b'1 0 0 rg 0 0 18 12 re f'


def pdf(data=content, filter_value=None, before='', page=None, catalog=None, pages=None):
    fields = f'/Length {len(data)} {before}'
    if filter_value is not None: fields += ' /Filter ' + filter_value
    extra = {4: b'<< ' + fields.encode() + b' >>\nstream\n' + data + b'\nendstream'}
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
        elif name == 'hex-empty':
            w, h, pages = struct.unpack('=III', result.stdout[:12])
            assert (w, h, pages) == (24, 16, 1)
            assert result.stdout[12:] == bytes([255]) * (w*h*4)
        else:
            assert result.stdout == baseline, name
print(f'PDF stream and dictionary regressions: {len(cases)} passed')
