#!/usr/bin/env python3
"""Check Type 3 object generations, numeric references and matrix advances."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_xref import PDF
from test_pdf_pages import document
from test_pdf_type3 import stream
from audit_pdf_corpus import read_ppm


def fixture(generation=0, matrix=None, indirect=False, resources='', charproc=None, content='BT /F 20 Tf 10 20 Td (AA) Tj ET'):
    matrix = matrix or [.001, 0, 0, .001, 0, 0]
    pdf = PDF()
    pdf.obj(1, b'<< /Type /Catalog /Pages 2 0 R >>')
    pdf.obj(2, b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>')
    pdf.obj(3, b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 120 90] /Contents 4 0 R /Resources << /Font << /F 5 0 R >> >> >>')
    pdf.obj(4, stream(content))
    if indirect:
        for i, value in enumerate([*matrix, 0, 0, 500, 700]):
            pdf.obj(10 + i, str(value).encode())
        font_matrix = ' '.join(f'{i} 0 R' for i in range(10, 16))
        bounds = ' '.join(f'{i} 0 R' for i in range(16, 20))
    else:
        font_matrix = ' '.join(map(str, matrix))
        bounds = '0 0 500 700'
    charproc = charproc if charproc is not None else f'6 {generation} R'
    pdf.obj(5, (f'<< /Type /Font /Subtype /Type3 /FontBBox [{bounds}] /FontMatrix [{font_matrix}] '
                f'/Encoding << /Differences [65 /A /B] >> /FirstChar 65 /LastChar 66 /Widths [600 600] '
                f'/CharProcs << /A {charproc} /B 6 {generation} R >> {resources} >>').encode())
    pdf.obj(6, stream('600 0 d0 0 0 500 700 re f'), generation)
    pdf.table()
    return bytes(pdf.data)


def main():
    probe = str(Path(sys.argv[1]).resolve()); checks = allocation_checks = references = 0
    positional = [arg for arg in sys.argv[2:] if not arg.startswith('--')]
    oom = str(Path(positional[0]).resolve()) if positional else None
    with tempfile.TemporaryDirectory(prefix='cupidimage-type3-objects-') as tmp:
        path = Path(tmp) / 'case.pdf'
        def render(data, reference=False):
            nonlocal allocation_checks, references
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), '0'], capture_output=True, timeout=30)
            assert result.returncode == 0, result.stderr
            if reference and '--reference' in sys.argv:
                prefix = Path(tmp) / 'reference'
                ref = subprocess.run(['pdftoppm', '-r', '96', '-aa', 'no', '-aaVector', 'no',
                                      '-singlefile', str(path), str(prefix)], capture_output=True, timeout=30)
                assert ref.returncode == 0, ref.stderr
                w, h, rgb = read_ppm(prefix.with_suffix('.ppm'))
                pixels = bytes(v for i, v in enumerate(result.stdout[12:]) if i % 4 != 3)
                assert (w, h) == (160, 120) and rgb == pixels
                references += 1
            if oom:
                faults = subprocess.run([oom, str(path), '0'], capture_output=True, text=True, timeout=60)
                assert faults.returncode == 0, (faults.stdout, faults.stderr)
                allocation_checks += int(faults.stdout.split(': ')[1].split()[0])
            return result.stdout
        baseline = render(fixture(), reference=True)
        for generation in (1, 2, 255, 256, 65535):
            assert render(fixture(generation=generation), reference=True) == baseline, generation
            checks += 1
        for matrix in ([.001, 0, 0, .001, 0, 0], [.001, .0003, .0002, .001, 0, 0],
                       [0, .001, -.001, 0, 0, 0], [-.001, 0, 0, .001, .5, 0]):
            # ISO 32000-1 Table 112 keeps only the transformed width's x component.
            expected = ''
            for x in (10, 10 + 600 * matrix[0] * 20):
                transform = [matrix[i] * 20 for i in range(6)]
                transform[4] += x
                transform[5] += 20
                expected += 'q ' + ' '.join(map(str, transform)) + ' cm 0 0 500 700 re f Q '
            vector = render(document('/MediaBox [0 0 120 90]', stream=expected))
            for indirect in (False, True):
                assert render(fixture(matrix=matrix, indirect=indirect), reference=matrix[0] >= 0 and matrix[2] <= 0) == vector, (matrix, indirect)
                checks += 1
        for value in ('null', '0 0 R', '90 0 R'):
            assert render(fixture(resources='/Resources ' + value)) == baseline, value
            checks += 1
        expected = render(fixture(generation=1, content='BT /F 20 Tf 22 20 Td (B) Tj ET'))
        for value in ('null', '0 0 R', '90 0 R', '6 0 R'):
            actual = render(fixture(generation=1, charproc=value, content='BT /F 20 Tf 10 20 Td (AB) Tj ET'))
            assert actual == expected, value
            checks += 1
    print(f'PDF Type 3 object and matrix checks: {checks} passed; {allocation_checks} allocation checks; {references} reference matches')


if __name__ == '__main__':
    main()
