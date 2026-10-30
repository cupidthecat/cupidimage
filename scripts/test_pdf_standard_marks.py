#!/usr/bin/env python3
"""Check spacing marks, fallback chains, PDF advances and allocation cleanup."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

from generate_pdf_standard_marks import SOURCE
from generate_pdf_standard_metrics import metrics, SOURCE as AFMS
from test_pdf_standard_fonts import fixture
from test_pdf_type3 import stream


def embedded_font(data, width, subtype='Type1C'):
    return {
        5: ('<< /Type /Font /Subtype /Type1 /BaseFont /CupidSpacingTest /FontDescriptor 10 0 R '
            '/FirstChar 65 /LastChar 65 /Widths [' + str(width) + '] '
            '/Encoding << /Differences [65 /space_uni0326] >> >>').encode(),
        10: b'<< /Type /FontDescriptor /FontName /CupidSpacingTest /Flags 32 '
            b'/FontBBox [-2000 -2000 3000 3000] /ItalicAngle 0 /Ascent 1000 /Descent -1000 '
            b'/CapHeight 700 /StemV 80 /FontFile3 11 0 R >>',
        11: stream(data, '/Subtype /' + subtype),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('probe', type=Path)
    parser.add_argument('allocator', type=Path)
    parser.add_argument('--sources', type=Path, help='Optional pinned original OTF directory')
    options = parser.parse_args()
    checks = allocations = original_checks = 0
    manifest = json.loads((SOURCE / 'manifest.json').read_text())
    with tempfile.TemporaryDirectory(prefix='cupid-spacing-marks-') as temporary:
        path = Path(temporary) / 'case.pdf'
        for row in manifest['fonts']:
            name = row['standard_name']
            width = metrics(AFMS / (name + '.afm'))[0]['commaaccent']
            subset = (SOURCE / row['file']).read_bytes()
            encoding = '/Encoding << /Differences [65 /commaaccent] >>'

            def render(content, embedded=False, attributes='', data=None, source='Type1C'):
                nonlocal checks
                extra = embedded_font(data or subset, width, source) if embedded else None
                if not embedded and '/MissingWidth' in attributes:
                    extra = {10: ('<< /Type /FontDescriptor /MissingWidth ' + attributes.split('/MissingWidth ')[1] + ' >>').encode()}
                    attributes = '/FontDescriptor 10 0 R'
                raw = fixture(content, name, encoding, attributes, extra)
                path.write_bytes(raw)
                result = subprocess.run([str(options.probe.resolve()), str(path), '0'],
                                        capture_output=True, timeout=30)
                assert result.returncode == 0, (name, result.stderr)
                checks += 1
                return result.stdout, raw

            for mode in range(8):
                content = f'.75 w BT /F 40 Tf {mode} Tr 20 55 Td (AAA) Tj ET'
                if mode >= 4:
                    content += ' 1 0 0 rg 0 0 120 90 re f'
                actual, raw = render(content)
                assert actual == render(content, True)[0], (name, mode, 'subset mismatch')
                if mode != 3:
                    assert any(value != 255 for value in actual[12:]), (name, mode, 'blank mark')
                if mode in (0, 2, 7):
                    path.write_bytes(raw)
                    result = subprocess.run([str(options.allocator.resolve()), str(path), '0'],
                                            capture_output=True, text=True, timeout=120)
                    assert result.returncode == 0, (name, mode, result.stdout, result.stderr)
                    allocations += int(result.stdout.split('checks')[0].split()[-1])
            for matrix in ('0 1 -1 0 65 10', '1 .25 .3 1 10 50', '-1 0 0 1 100 55'):
                content = f'BT /F 30 Tf {matrix} Tm (AAA) Tj ET'
                assert render(content)[0] == render(content, True)[0], (name, matrix)
            content = 'BT /F 30 Tf 2 Tc 3 Tw 75 Tz 4 Ts 20 55 Td [(A) -250 (AA)] TJ ET'
            assert render(content)[0] == render(content, True)[0], (name, 'text state')
            # Explicit/descriptor metrics and AFM defaults control advances, not
            # the spacing subset's own charstring width.
            for advance in (width, 0, -200, 333, 700):
                attributes = '' if advance == width else f'/FirstChar 65 /LastChar 65 /Widths [{advance}]'
                content = f'BT /F 30 Tf 3 Tr {60-2*advance*.03:.8f} 30 Td (AA) Tj /Marker 30 Tf 0 Tr (A) Tj ET'
                assert render(content, attributes=attributes)[0] == render('60 30 12 18 re f')[0], (name, advance)
            content = 'BT /F 30 Tf 3 Tr 39 30 Td (AA) Tj /Marker 30 Tf 0 Tr (A) Tj ET'
            assert render(content, attributes='/MissingWidth 350')[0] == render('60 30 12 18 re f')[0], name
            # Three adjacent glyphs select the compact, extended, and mark programs.
            content = 'BT /F 30 Tf 15 55 Td (ABCABC) Tj ET'
            mixed = '/Encoding << /Differences [65 /A /abreve /commaaccent] >>'
            path.write_bytes(fixture(content, name, mixed))
            actual = subprocess.check_output([str(options.probe.resolve()), str(path), '0'])
            widths = metrics(AFMS / (name + '.afm'))[0]
            # Independent positioned draws preserve the same baseline and advances.
            pieces = []
            x = 15.0
            for glyph in ['A', 'abreve', 'commaaccent'] * 2:
                pieces.append((glyph, x))
                x += widths[glyph] * .03
            reference = None
            # Rendering each isolated glyph over white then combining black ink
            # avoids sharing the fallback-chain dispatch of the mixed string.
            for glyph, x in pieces:
                encoded = f'/Encoding << /Differences [65 /{glyph}] >>'
                path.write_bytes(fixture(f'BT /F 30 Tf 1 0 0 1 {x:.8f} 55 Tm (A) Tj ET', name, encoded))
                pixels = subprocess.check_output([str(options.probe.resolve()), str(path), '0'])
                reference = pixels if reference is None else bytes(min(a, b) for a, b in zip(reference, pixels))
            assert actual == reference, (name, 'three-program chain')
            checks += 7
            if options.sources:
                original = options.sources / row['original_file']
                assert hashlib.sha256(original.read_bytes()).hexdigest() == row['original_sha256']
                for mode in (0, 2, 7):
                    content = f'.75 w BT /F 40 Tf {mode} Tr 20 55 Td (AAA) Tj ET'
                    if mode == 7:
                        content += ' 1 0 0 rg 0 0 120 90 re f'
                    assert render(content)[0] == render(content, True, data=original.read_bytes(), source='OpenType')[0], (name, mode, 'original mismatch')
                    original_checks += 1
    print(f'Standard spacing marks: {checks} rendering/advance checks; {allocations} allocation/cache checks; '
          f'{original_checks} original-program equivalences; 8 named glyph gaps filled, no full-font parity claim')


if __name__ == '__main__':
    main()
