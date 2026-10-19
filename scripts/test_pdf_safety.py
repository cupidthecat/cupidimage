#!/usr/bin/env python3
"""Check oversized PDF metadata through the public page APIs."""
import pathlib
import re
import subprocess
import sys
import tempfile

from test_pdf_pages import document


def main():
    probe = str(pathlib.Path(sys.argv[1]).resolve())
    base = document('/MediaBox [0 0 18 12]')
    cases = [('valid', base, False)]
    for value in ('2147483648', '999999999999', '18446744073709551615',
                  '18446744073709551616', '9' * 200):
        number = value.encode()
        for name, pdf in (
            ('startxref', re.sub(rb'startxref\n\d+', b'startxref\n' + number, base)),
            ('xref-first', base.replace(b'xref\n0 5\n', b'xref\n' + number + b' 5\n')),
            ('xref-count', base.replace(b'xref\n0 5\n', b'xref\n0 ' + number + b'\n')),
            ('root', base.replace(b'/Root 1 0 R', b'/Root ' + number + b' 0 R')),
            ('pages', document('/MediaBox [0 0 18 12]', extra={
                1: f'<< /Type /Catalog /Pages {value} 0 R >>'})),
            ('kids', document('/MediaBox [0 0 18 12]', extra={
                2: f'<< /Type /Pages /Count 1 /Kids [{value} 0 R] >>'})),
            ('contents', document(f'/MediaBox [0 0 18 12] /Contents {value} 0 R',
                extra={3: f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents {value} 0 R >>'})),
        ):
            cases.append((name + '-' + value[:20], pdf, True))
        # Font metadata is optional; malformed maps must fall back safely.
        cases.append(('font-' + value[:20], document(
            f'/MediaBox [0 0 18 12] /Resources << /Font << /F1 {value} 0 R >> >>'), False))
    cases += [
        ('xref-range-overflow', base.replace(b'xref\n0 5\n', b'xref\n2147483647 5\n'), True),
        ('xref-zero-count', base.replace(b'xref\n0 5\n', b'xref\n0 0\n'), True),
        ('kids-generation', document('/MediaBox [0 0 18 12]', extra={
            2: '<< /Type /Pages /Count 1 /Kids [3 999999999999  R] >>'}), True),
    ]
    for value in ('', '-1', '123x'):
        cases.append(('invalid-offset-' + value[:10], re.sub(
            rb'startxref\n\d+', b'startxref\n' + value.encode(), base), True))
    offset = re.search(rb'startxref\n(\d+)', base).group(1)
    cases.append(('long-valid-offset', re.sub(rb'startxref\n\d+',
        b'startxref\n' + b'0' * 150 + offset, base), False))
    for contents in ('null', '[]', '[ % empty\n ]'):
        cases.append(('empty-contents-' + contents, document(extra={
            3: '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Contents ' + contents + ' >>'}), False))
    for value in ('18446744073709551616', '9' * 200):
        cases.append(('entry-offset-' + value[:20], base.replace(
            b'0000000000 65535 f', value.encode() + b' 65535 f'), True))
    cases.append(('entry-generation', base.replace(b'65535 f', b'999999999999 f'), True))
    failures = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-pdf-safety-') as directory:
        for index, (name, pdf, reject) in enumerate(cases):
            path = pathlib.Path(directory) / f'{index}.pdf'
            path.write_bytes(pdf)
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=15)
            wanted = 3 if reject else 0
            if result.returncode != wanted or (reject and not result.stderr):
                failures += 1
                print('FAIL', name, result.returncode, result.stderr[:1000], file=sys.stderr)
    print(f'{len(cases)} PDF metadata checks, {failures} failures')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
