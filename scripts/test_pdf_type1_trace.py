#!/usr/bin/env python3
"""Check Type 1 hint events without changing unhinted glyph geometry."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_type1 import fixture
from test_pdf_type1_charstrings import command, WIDTH


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='cupid-type1-trace-') as tmp:
        path = Path(tmp) / 'font.bin'

        def run(code, events=(), diagnostics=0, ok=True, **kwargs):
            nonlocal checks
            data, clear, encrypted = fixture(glyphs=[('.notdef', WIDTH + b'\x0e'), ('A', code)], **kwargs)
            path.write_bytes(data)
            args = [probe, str(path), str(clear), str(encrypted)]
            result = subprocess.run(args + ['--hint-outline'], capture_output=True, text=True, timeout=10)
            assert result.returncode == (0 if ok else 3), (result.returncode, result.stderr)
            if ok:
                lines = result.stdout.splitlines()
                assert f'hint-trace 1 {len(events)} {diagnostics}' in lines, lines
                actual = [tuple(map(float, line.split()[2:])) for line in lines
                          if line.startswith('hint-event 1 ')]
                expected = [tuple(event) + (0,) * (9 - len(event)) for event in events]
                assert actual == expected, (actual, expected)
                plain = subprocess.run(args + ['--outline'], capture_output=True, text=True,
                                       check=True, timeout=10).stdout.splitlines()
                filtered = [line for line in lines if not line.startswith(('hint-event ', 'hint-trace '))]
                assert plain == filtered, 'Tracing changed unhinted geometry'
            checks += 1

        move = command([0, 0], 21)
        line = command([100, 0], 5)
        end = b'\x0e'
        run(WIDTH + move + line + end)
        for axis, op in ((0, 1), (1, 3)):
            run(command([20, 30, 600, 0], (12, 7)) + command([10, 40], op) + move + end,
                [(0, 0, axis, 10 + (20 if axis else 30), 40)])
            run(WIDTH + command([10, 40], op) + move + line + command([50, 20], op) + end,
                [(0, 0, axis, 10, 40), (0, 2, axis, 50, 20)])
            for width in (-20, -21, -22):
                run(WIDTH + command([100, width], op) + move + end,
                    [(0, 0, axis, 100, width)], int(axis or width == -22))
        for axis, op in ((0, (12, 2)), (1, (12, 1))):
            run(command([20, 30, 600, 0], (12, 7)) + command([90, 10, 10, 10, 50, 10], op) + move + end,
                [(1, 0, axis, 90 + (20 if axis else 30), 10,
                  10 + (20 if axis else 30), 10, 50 + (20 if axis else 30), 10)])
        run(WIDTH + command([0], 10) + move + command([0, 1, 3], (12, 16)) +
            bytes([12, 17, 10]) + line + end,
            [(0, 0, 0, 10, 20), (2, 1, 0), (0, 1, 0, 10, 20)],
            subrs=[command([10, 20], 1) + b'\x0b'])
        run(WIDTH + bytes([12, 0]) + move + line + bytes([12, 0]) + end,
            [(3, 0, 0), (3, 2, 0)])
        # Each declaration is absolute relative to the sidebearing, not a delta from the prior stem.
        run(WIDTH + command([10, 20], 1) + command([30, 40], 1) + move + end,
            [(0, 0, 0, 10, 20), (0, 0, 0, 30, 40)])
        for n in (16, 17, 32, 33, 96, 97):
            run(WIDTH + command([10, 20], 1) * n + move + end,
                [(0, 0, 0, 10, 20)] * n)
        subrs = [command([3, 0], (12, 16)) + bytes([12, 17, 12, 17, 12, 33, 11]),
                 command([0, 1], (12, 16)) + b'\x0b', command([0, 2], (12, 16)) + b'\x0b']
        flex = WIDTH + move + command([1], 10)
        for x, y in [(50, 0), (-35, 0), (10, 10), (25, 0), (25, 0), (10, -10), (15, 0)]:
            flex += command([x, y], 21) + command([2], 10)
        flex += command([50, 100, 0, 0], 10) + end
        run(flex, [(4, 1, 0, 50, 100, 0)], subrs=subrs)
        for code in (WIDTH + command([10], 1) + end, WIDTH + command([10, 20], 1),
                     WIDTH + command([1], 10) + end):
            run(code, ok=False)
    print(f'Type 1 hint trace: {checks} authored checks passed; no grid fitting')


if __name__ == '__main__':
    main()
