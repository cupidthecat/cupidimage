#!/usr/bin/env python3
"""Verify full-length decoded font resource names, scopes and state restoration."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_graphics_fonts import fixture
from test_pdf_type3 import stream, fixture as type3_fixture
from audit_pdf_corpus import read_ppm


def escaped(name):
    return ''.join('#' + format(c, '02X') for c in name.encode('latin1'))


def named(length):
    return 'F' + ('0123456789abcdef' * 8)[:length-1] if length else ''


def cases():
    box = '20 30 12 18 re f'
    for length in (0, 1, 31, 32, 63, 64, 126, 127):
        name = named(length)
        for resource, selection in ((name, name), (escaped(name), name), (name, escaped(name))):
            yield f'length-{length}', fixture('BT /'+selection+' 30 Tf 20 30 Td (A) Tj ET', fonts='/'+resource+' 5 0 R'), fixture(box), True
    for name in ('F slash/name', 'F#()<>[]{}%', '\xff\x80' + 'x'*125):
        name = escaped(name)
        yield 'escaped-bytes', fixture('BT /'+name+' 30 Tf 20 30 Td (A) Tj ET', fonts='/'+name+' 5 0 R'), fixture(box), True
    prefix = named(126)
    a, b = prefix+'A', prefix+'B'
    second = b'<< /Type /Font /Subtype /TrueType /BaseFont /CupidImageTestShapes /FontDescriptor 8 0 R /Encoding << /BaseEncoding /WinAnsiEncoding /Differences [65 /B] >> /FirstChar 65 /LastChar 65 /Widths [1000] >>'
    fonts = '/'+a+' 5 0 R /'+b+' 12 0 R'
    extra = {12: second}
    oracle = fixture('BT /F 30 Tf 20 30 Td (ABA) Tj ET')
    content = 'BT /'+a+' 30 Tf 20 30 Td (A) Tj q /'+b+' 30 Tf (A) Tj Q (A) Tj ET'
    yield 'common-prefix-and-qQ', fixture(content, fonts=fonts, extra=extra), oracle, True
    for mode in range(8):
        content = f'.75 w BT /{a} 30 Tf {mode} Tr 20 30 Td (A) Tj ET'
        short = f'.75 w BT /F 30 Tf {mode} Tr 20 30 Td (A) Tj ET'
        if mode >= 4:
            content += ' 1 0 0 rg 0 0 120 90 re f'
            short += ' 1 0 0 rg 0 0 120 90 re f'
        yield f'mode-{mode}', fixture(content, fonts=fonts, extra=extra), fixture(short), False
    page = ('<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << /'+a+' 5 0 R >> /XObject << /X 13 0 R >> >> >>').encode()
    form = stream('BT /'+a+' 30 Tf 50 30 Td (A) Tj ET', '/Type /XObject /Subtype /Form /BBox [0 0 120 90] /Resources << /Font << /'+a+' 12 0 R >> >>')
    content = 'BT /'+a+' 30 Tf 20 30 Td (A) Tj ET /X Do BT 80 30 Td (A) Tj ET'
    yield 'form-font-scope', fixture(content, extra={3: page, 12: second, 13: form}), oracle, True
    content = 'BT /'+a+' 30 Tf 20 30 Td (A) Tj q /Use gs (A) Tj Q (A) Tj ET'
    yield 'graphics-font-and-named-font', fixture(content, states='/Use << /Font [12 0 R 30] >>', fonts=fonts, extra=extra), oracle, True
    text = 'BT /F 30 Tf 20 30 Td (A) Tj ET'
    yield 'empty-Type3-glyph-name', type3_fixture(text, encoding='<< /Differences [65 /] >>', charprocs='/ 6 0 R /B 7 0 R'), type3_fixture(text), True
    page = b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /ColorSpace << / /DeviceRGB >> >> >>'
    yield 'empty-color-resource', fixture('/ cs 1 0 0 scn '+box, extra={3: page}), fixture('1 0 0 rg '+box), True
    page = b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /ColorSpace << /S [/Separation / /DeviceRGB << /FunctionType 2 /Domain [0 1] /C0 [1 1 1] /C1 [1 0 0] /N 1 >>] >> >> >>'
    yield 'empty-colorant-name', fixture('/S cs 1 scn '+box, extra={3: page}), fixture('1 0 0 rg '+box), True
    yield 'empty-graphics-state-name', fixture('/ gs '+box, states='/ << /ca 1 >>'), fixture(box), True
    # Resource names and text selection are decoded independently, including collisions.
    content = 'BT /'+escaped(a)+' 30 Tf 20 30 Td (A) Tj /'+escaped(b)+' 30 Tf (A) Tj /'+a+' 30 Tf (A) Tj ET'
    yield 'escaped-common-prefix', fixture(content, fonts=fonts, extra=extra), oracle, True


def main():
    probe, allocator = [str(Path(a).resolve()) for a in sys.argv[1:3]]
    checks = references = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupidimage-font-resources-') as tmp:
        path = Path(tmp)/'case.pdf'
        for label, actual, expected, compare in cases():
            output = []
            for data in (expected, actual):
                path.write_bytes(data)
                result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
                assert result.returncode == 0, (label, result.stderr)
                output.append(result.stdout)
            assert output[0] == output[1], label
            checks += 1
            if compare and '--reference' in sys.argv:
                prefix = Path(tmp)/'reference'
                result = subprocess.run(['pdftoppm', '-r', '96', '-aa', 'no', '-aaVector', 'no', '-singlefile', str(path), str(prefix)], capture_output=True, timeout=30)
                assert result.returncode == 0, (label, result.stderr)
                w, h, pixels = read_ppm(prefix.with_suffix('.ppm'))
                assert (w, h) == (160, 120) and pixels == bytes(v for i,v in enumerate(output[1][12:]) if i%4 != 3), label
                references += 1
            result = subprocess.run([allocator, str(path), '0'], capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, (label, result.stdout, result.stderr)
            allocations += int(result.stdout.split('checks')[0].split()[-1])
        for name in (named(128), escaped(named(128)), 'F#00bad'):
            path.write_bytes(fixture('BT /'+name+' 30 Tf 20 30 Td (A) Tj ET', fonts='/'+name+' 5 0 R'))
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert result.returncode != 0, name
            checks += 1
        for subtype, base in (('Type1', 'Helvetica'), ('TrueType', 'CupidImageTestShapes')):
            for encoding in ('/', '<< /BaseEncoding / >>'):
                font = f'<< /Type /Font /Subtype /{subtype} /BaseFont /{base} /Encoding {encoding} /FontDescriptor 8 0 R >>'.encode()
                if subtype == 'Type1':
                    font = f'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding {encoding} >>'.encode()
                path.write_bytes(fixture('BT /F 30 Tf 20 30 Td (A) Tj ET', extra={5: font}))
                result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
                assert result.returncode != 0, (subtype, encoding)
                checks += 1
    print(f'PDF font resource names: {checks} checks; {references} independent comparisons; {allocations} allocation checks')


if __name__ == '__main__':
    main()
