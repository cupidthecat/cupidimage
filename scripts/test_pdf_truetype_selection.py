#!/usr/bin/env python3
"""Check PDF cmap selection and post-name fallback using conflicting font tables."""
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
from test_pdf_embedded_fonts import FONT, fixture
from test_pdf_truetype import replace_tables
from audit_pdf_corpus import read_ppm
from test_pdf_type3 import fixture as type3_pdf
from test_pdf_cff_render import fixture as cff_pdf
from test_pdf_type1_render import fixture as type1_pdf


def cmap(records):
    offset = 4 + 8 * len(records)
    entries = bytearray(); payload = bytearray()
    for platform, encoding, code, glyph in records:
        table = (struct.pack('>HHIIIIII', 12, 0, 28, 0, 1, code, code, glyph)
                 if (platform, encoding) == (3, 10) else struct.pack('>6H', 6, 12, 0, code, 1, glyph))
        entries += struct.pack('>HHI', platform, encoding, offset)
        payload += table
        offset += len(table)
    return struct.pack('>HH', 0, len(records)) + entries + payload


def post(name, glyph):
    names = json.loads(Path('tests/fixtures/pdf-post-names.json').read_text())['names']
    ids = [names.index(n) for n in ('.notdef', 'a', 'b', 'c', 'd', 'e', 'f', 'g')]
    ids[glyph] = names.index(name) if name in names else 258
    extra = b'' if name in names else bytes([len(name)]) + name.encode()
    return struct.pack('>I', 0x20000) + bytes(28) + struct.pack('>9H', 8, *ids) + extra


def cases():
    yield 'unicode-before-roman', [(3, 1, 65, 1), (1, 0, 65, 2)], 'A', 3, 1, True
    yield 'post-before-roman-fallback', [(3, 1, 66, 2), (1, 0, 65, 2)], 'A', 1, 1, True
    yield 'zero-unicode-to-post', [(3, 1, 65, 0), (1, 0, 65, 2)], 'A', 1, 1, True
    yield 'post-before-full-repertoire-fallback', [(3, 1, 66, 2), (3, 10, 65, 2)], 'A', 1, 1, True
    yield 'roman-before-full-repertoire', [(1, 0, 65, 1), (3, 10, 65, 2)], 'A', 2, 1, True
    yield 'roman-before-post', [(1, 0, 65, 1)], 'A', 2, 1, True
    yield 'missing-roman-to-post', [(1, 0, 66, 2), (3, 10, 65, 2)], 'A', 1, 1, True
    yield 'unicode-before-post', [(3, 1, 65, 1)], 'A', 2, 1, True
    yield 'full-repertoire-extension', [(3, 10, 65, 1)], 'A', 2, 1, False
    yield 'unicode-platform-extension', [(0, 3, 65, 1)], 'A', 2, 1, False
    yield 'mac-omega-name', [(1, 0, 189, 1)], 'Omega', 2, 1, True
    yield 'unicode-omega-name', [(3, 1, 0x2126, 1), (1, 0, 189, 2)], 'Omega', 2, 1, True
    for name, code in (('notequal', 173), ('infinity', 176), ('lessequal', 178),
                       ('greaterequal', 179), ('partialdiff', 182), ('summation', 183),
                       ('product', 184), ('pi', 185), ('integral', 186), ('radical', 195),
                       ('approxequal', 197), ('Delta', 198), ('lozenge', 215), ('Euro', 219),
                       ('apple', 240)):
        yield 'mac-name-' + name, [(1, 0, code, 1)], name, 2, 1, name != 'Euro'

    for name in ('A.alt', 'uni0041', 'u0041', 'customShape'):
        yield 'post-name-' + name, [(3, 1, 66, 2), (1, 0, 65, 2)], name, 1, 1, True


def documents():
    for name, records, glyph_name, post_glyph, expected, compare in cases():
        font = replace_tables(FONT, {b'cmap': cmap(records), b'post': post(glyph_name, post_glyph)})
        direct = ('[65 /' + glyph_name + ']').encode()
        for form, differences, extra in (
            ('direct', direct.decode(), {}),
            ('indirect-array', '40 0 R', {40: direct}),
            ('indirect-members', '[40 0 R 41 0 R]', {40: b'65', 41: ('/' + glyph_name).encode()}),
        ):
            encoding = '<< /BaseEncoding /WinAnsiEncoding /Differences ' + differences + ' >>'
            yield name + '-' + form, font, encoding, extra, expected, compare


def main():
    probe = str(Path(sys.argv[1]).resolve())
    positional = [arg for arg in sys.argv[2:] if not arg.startswith('--')]
    oom = str(Path(positional[0]).resolve()) if positional else None
    checks = references = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-tt-selection-') as tmp:
        path = Path(tmp) / 'case.pdf'
        for name, font, encoding, extra, expected, compare in documents():
            pixels = []
            for content in ('20 30 12 18 re f', 'BT /F 30 Tf 20 30 Td (A) Tj ET'):
                assert expected == 1
                path.write_bytes(fixture(content, font_data=font, encoding=encoding, extra=extra))
                run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
                assert run.returncode == 0, (name, run.stderr)
                pixels.append(run.stdout)
            assert pixels[0] == pixels[1], (name, sum(a != b for a, b in zip(*pixels)))
            if '--reference' in sys.argv and compare:
                prefix = Path(tmp) / 'reference'
                ref = subprocess.run(['pdftoppm', '-r', '96', '-aa', 'no', '-aaVector', 'no',
                                      '-singlefile', str(path), str(prefix)], capture_output=True, timeout=30)
                assert ref.returncode == 0, (name, ref.stderr)
                w, h, rgb = read_ppm(prefix.with_suffix('.ppm'))
                actual = bytes(v for i, v in enumerate(pixels[1][12:]) if i % 4 != 3)
                assert (w, h) == (160, 120) and rgb == actual, name
                references += 1
            if oom:
                faults = subprocess.run([oom, str(path), '0'], capture_output=True, text=True, timeout=60)
                assert faults.returncode == 0, (name, faults.stdout, faults.stderr)
                allocations += int(faults.stdout.split(': ')[1].split()[0])
            checks += 1
        for label, make in (('Type3', type3_pdf), ('CFF', cff_pdf), ('Type1', type1_pdf)):
            content = 'BT /F 30 Tf 20 30 Td (AA) Tj ET'
            path.write_bytes(make(content, encoding='<< /Differences [65 /A] >>'))
            baseline = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert baseline.returncode == 0, (label, baseline.stderr)
            for differences, extra in (
                ('[90 0 R 91 0 R]', {90: b'65', 91: b'/A'}),
                ('92 0 R', {90: b'65', 91: b'/A', 92: b'[90 0 R 91 0 R]'}),
            ):
                path.write_bytes(make(content, encoding='<< /Differences ' + differences + ' >>', extra=extra))
                run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
                assert run.returncode == 0 and run.stdout == baseline.stdout, (label, run.stderr)
                if '--reference' in sys.argv:
                    prefix = Path(tmp) / 'reference'
                    ref = subprocess.run(['pdftoppm', '-r', '96', '-aa', 'no', '-aaVector', 'no',
                                          '-singlefile', str(path), str(prefix)], capture_output=True, timeout=30)
                    assert ref.returncode == 0, (label, ref.stderr)
                    w, h, rgb = read_ppm(prefix.with_suffix('.ppm'))
                    actual = bytes(v for i, v in enumerate(run.stdout[12:]) if i % 4 != 3)
                    assert (w, h) == (160, 120) and rgb == actual, label
                    references += 1
                if oom:
                    faults = subprocess.run([oom, str(path), '0'], capture_output=True, text=True, timeout=60)
                    assert faults.returncode == 0, (label, faults.stdout, faults.stderr)
                    allocations += int(faults.stdout.split(': ')[1].split()[0])
                checks += 1
        for differences, extra in (
            ('[65.0 /A]', {}), ('[65junk /A]', {}), ('[65 null]', {}),
            ('[65 90 0 R]', {}), ('[90 0 R /A]', {90: b'90 0 R'}),
            ('[255 /A /B]', {}), ('90 0 R', {90: b'90 0 R'}),
        ):
            encoding = '<< /Differences ' + differences + ' >>'
            path.write_bytes(fixture('BT /F 30 Tf 20 30 Td (A) Tj ET', encoding=encoding, extra=extra))
            run = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert run.returncode == 3, (differences, run.stderr)
            checks += 1
    print(f'PDF TrueType selection: {checks} checks; {references} reference matches; {allocations} allocation checks')


if __name__ == '__main__':
    main()
