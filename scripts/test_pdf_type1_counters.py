#!/usr/bin/env python3
"""Check Type 1 counter-data chunks, traces, compatibility rendering and bounds."""
from pathlib import Path
import argparse
import itertools
import subprocess
import tempfile
from test_pdf_type1 import fixture
from test_pdf_type1_charstrings import command, WIDTH, BOX
from test_pdf_type1_render import fixture as pdf

DATA = [2, 0, 20, 100, -20, 200, 30, 150, -30,
        1, 10, 15, 80, 15, 100, -15]
EVENTS = [(0, 0, 20, 0), (0, 100, -20, 0), (0, 200, 30, 1),
          (0, 150, -30, 1), (1, 10, 15, 0), (1, 80, 15, 0), (1, 100, -15, 0)]
SHAPE = BOX[len(WIDTH):]


def counter(data=DATA, chunk=22):
    code = b''
    end = len(data)
    if not end:
        return command([0, 13], (12, 16))
    while end:
        start = max(0, end-chunk)
        values = data[start:end]
        for value in values:
            code += (command([int(value)], ()) if int(value) == value else
                     command(list(value.as_integer_ratio()), (12, 12)))
        code += command([len(values), 12 if start else 13], (12, 16))
        end = start
    return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    parser.add_argument('pages', type=Path)
    parser.add_argument('allocator', type=Path, nargs='?')
    parser.add_argument('--reference', action='store_true')
    args = parser.parse_args()
    probe, pages = str(args.probe.resolve()), str(args.pages.resolve())
    checks = page_checks = references = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupid-type1-counters-') as temporary:
        root = Path(temporary)
        path = root/'font.bin'
        def run(code, valid=True, expected=EVENTS, **kwargs):
            nonlocal checks
            data, clear, encrypted = fixture(glyphs=[('.notdef', WIDTH+b'\x0e'), ('A', code)], **kwargs)
            path.write_bytes(data)
            args = [probe, str(path), str(clear), str(encrypted)]
            result = subprocess.run(args+['--hint-outline'], capture_output=True, text=True, timeout=15)
            assert result.returncode == (0 if valid else 3), (result.stderr, result.stdout, code.hex())
            checks += 1
            if not valid:
                return
            lines = result.stdout.splitlines()
            events = [tuple(map(float, line.split()[2:])) for line in lines if line.startswith('hint-event 1 ')]
            wanted = [(5, 0, axis, delta, width, group, int(width < 0), 0, 0)
                      for axis, delta, width, group in expected]
            assert events == wanted, (events, wanted)
            assert f'hint-trace 1 {len(wanted)} 2' in lines
            plain = subprocess.check_output(args+['--outline'], text=True, timeout=15).splitlines()
            assert plain == [line for line in lines if not line.startswith(('hint-event ', 'hint-trace '))]
        for chunk, iv in itertools.product(range(1, 23), (-1, 0, 4)):
            run(WIDTH+counter(chunk=chunk)+SHAPE, len_iv=iv)
        run(WIDTH+command([0, 12], (12, 16))+counter()+SHAPE)
        run(WIDTH+counter([0, 0])+SHAPE, expected=[])
        run(WIDTH+counter([1, .5, 20.5, 100.25, -20.25, 0], chunk=3)+SHAPE,
            expected=[(0, .5, 20.5, 0), (0, 100.25, -20.25, 0)])
        run(WIDTH+command([0], 10)+SHAPE, subrs=[counter(chunk=3)+b'\x0b'])
        run(command([20, 30, 600, 0], (12, 7))+counter()+SHAPE)
        # Each completed block has exactly two orientation lists and terminated groups.
        for data in ([], [0], [-1, 0], [1, 0], [1, 0, 20, 0], [0, 1],
                     [0, 0, 1], [1, 0, 20, 1, 20, 0], [65537, 0], [0, -1]):
            run(WIDTH+counter(data)+SHAPE, valid=False)
        for code in (counter()+WIDTH+SHAPE,
                     WIDTH+command([0, 0], 21)+counter()+b'\x0e',
                     WIDTH+command([0, 20], 1)+counter()+SHAPE,
                     WIDTH+counter()+counter()+SHAPE,
                     WIDTH+command([0, 12], (12, 16))+SHAPE,
                     WIDTH+command([1, 1, 12], (12, 16))+b'\x0e',
                     WIDTH+counter()+bytes([12, 17])+SHAPE,
                     WIDTH+command([99, 0, 0, 2, 13], (12, 16))+SHAPE,
                     WIDTH+command([0, 4], (12, 16))+SHAPE):
            run(code, valid=False)
        paths = [root/'counter.pdf', root/'plain.pdf']
        for aa, hint, mode, tm in itertools.product((0, 1), (0, 1), range(8),
                                                    ('1 0 0 1 21 30', '1 .25 .125 1 21 30', '0 1 -1 0 60 30')):
            content = f'.5 w BT /F 30 Tf {mode} Tr {tm} Tm (A) Tj ET'
            if mode >= 4:
                content += ' 1 0 0 rg 0 0 120 90 re f'
            for target, code in zip(paths, (WIDTH+counter(chunk=3)+SHAPE, BOX)):
                target.write_bytes(pdf(content, glyphs=[('.notdef', WIDTH+b'\x0e'), ('A', code)]))
            results = [subprocess.check_output([pages, str(p), '0', str(aa), '0', '0', str(hint)], timeout=15) for p in paths]
            assert results[0] == results[1], (aa, hint, mode, tm)
            page_checks += 1
            if args.reference and not hint:
                for p in paths:
                    subprocess.run(['pdftoppm', '-r', '96', '-aa', 'yes' if aa else 'no',
                                    '-aaVector', 'yes' if aa else 'no', '-singlefile', str(p),
                                    str(p.with_suffix(''))], check=True, capture_output=True, timeout=15)
                assert paths[0].with_suffix('.ppm').read_bytes() == paths[1].with_suffix('.ppm').read_bytes()
                references += 1
        if args.allocator:
            for aa, hint, invalid in itertools.product((0, 1), (0, 1), (0, 1)):
                code = WIDTH+(command([0, 12], (12, 16)) if invalid else counter(chunk=3))+SHAPE
                paths[0].write_bytes(pdf('BT /F 30 Tf 21 30 Td (A) Tj ET',
                                        glyphs=[('.notdef', WIDTH+b'\x0e'), ('A', code)]))
                result = subprocess.run([str(args.allocator.resolve()), str(paths[0]), str(invalid),
                                         str(aa), '0', '0', str(hint)], capture_output=True,
                                        text=True, check=True, timeout=120)
                allocations += int(result.stdout.split('checks')[0].split()[-1])
    print(f'Type 1 counter data: {checks} program/trace checks; {page_checks} paired pages; '
          f'{references} independent pairs; {allocations} allocation checks; '
          'designer counter priorities remain unapplied')


if __name__ == '__main__':
    main()
