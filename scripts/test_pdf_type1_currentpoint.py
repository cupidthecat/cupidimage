#!/usr/bin/env python3
"""Keep logical charstring position changes out of the painted path."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_type1_charstrings import command, WIDTH
from test_pdf_type1_render import fixture


def main():
    probe = str(Path(sys.argv[1]).resolve())
    base = WIDTH + command([0, 0], 21) + command([100, 0], 5)
    changed = base
    for value in (200, 0):
        changed += command([value, 1, 3], (12, 16)) + bytes([12, 17])
    changed += bytes([12, 33]) + command([0, 100], 5) + command([-200, 0], 5) + b'\x09\x0e'
    expanded = base + command([100, 100], 5) + command([-200, 0], 5) + b'\x09\x0e'
    checks = references = 0
    with tempfile.TemporaryDirectory(prefix='cupid-type1-currentpoint-') as tmp:
        paths = [Path(tmp) / name for name in ('changed.pdf', 'expanded.pdf')]
        for aa in (False, True):
            for mode in range(8):
                for matrix in ('1 0 0 1 21 30', '1 .25 .125 1 21 30', '0 1 -1 0 60 30'):
                    content = f'.5 w BT /F 75 Tf {mode} Tr {matrix} Tm (A) Tj ET'
                    if mode >= 4:
                        content += ' 0 0 1 rg 0 0 120 90 re f'
                    for path, code in zip(paths, (changed, expanded)):
                        path.write_bytes(fixture(content, glyphs=[('.notdef', WIDTH+b'\x0e'), ('A', code)]))
                    for hint in (False, True):
                        args = ['--pdf-text-aa'] if aa else []
                        if hint:
                            args.append('--pdf-type1-hinting')
                        results = [subprocess.run([probe, str(p), '0', *args], capture_output=True,
                                                  check=True, timeout=20).stdout for p in paths]
                        assert results[0] == results[1], (aa, hint, mode, matrix)
                        checks += 1
                    if '--reference' in sys.argv:
                        for p in paths:
                            subprocess.run(['pdftoppm', '-r', '96', '-aa', 'yes' if aa else 'no',
                                            '-aaVector', 'yes' if aa else 'no', '-singlefile',
                                            str(p), str(p.with_suffix(''))], capture_output=True,
                                           check=True, timeout=20)
                        assert paths[0].with_suffix('.ppm').read_bytes() == paths[1].with_suffix('.ppm').read_bytes(), (aa, mode, matrix)
                        references += 1
    print(f'Type 1 current-point paths: {checks} native equivalences; {references} independent equivalences')


if __name__ == '__main__':
    main()
