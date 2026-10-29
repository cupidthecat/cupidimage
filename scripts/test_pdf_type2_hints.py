#!/usr/bin/env python3
"""Check Type 2 stem and mask capture independently of outline construction."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_cff import fixture
from test_pdf_type2 import command


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='cupid-type2-hints-') as tmp:
        path = Path(tmp) / 'font.cff'

        def run(code, stems=(), events=(), diagnostics=0, ok=True, **kwargs):
            nonlocal checks
            path.write_bytes(fixture(charset=0, strings=(), glyphs=2,
                                    programs=[b'\x0e', code], **kwargs))
            result = subprocess.run([probe, str(path), '--hint-outline'], capture_output=True,
                                    text=True, timeout=10)
            assert result.returncode == (0 if ok else 3), result.stderr
            if ok:
                lines = result.stdout.splitlines()
                assert f'hint-program 1 {len(stems)} {diagnostics}' in lines, lines
                actual = [tuple(map(float, v.split()[2:])) for v in lines if v.startswith('stem 1 ')]
                assert actual == list(stems), (actual, stems)
                actual_events = [v.removeprefix('hint-event 1 ') for v in lines
                                 if v.startswith('hint-event 1 ')]
                assert actual_events == list(events), (actual_events, events)
                plain = subprocess.run([probe, str(path), '--outline'], capture_output=True,
                                       text=True, timeout=10, check=True).stdout.splitlines()
                filtered = [v for v in lines if not v.startswith(('hint-program', 'stem ', 'hint-event'))]
                assert filtered == plain, 'Hint capture changed the unhinted outline'
            checks += 1

        move = command([0, 0], 21)
        line = command([100, 0, 0, 100, -100, 0], 5)
        end = b'\x0e'
        run(move + line + end)
        for op in (1, 18, 3, 23):
            axis = int(op in (3, 23))
            run(command([700, 10, 20, 30, 40], op) + move + end,
                [(axis, 10, 20), (axis, 60, 40)])
            for width in (-20, -21, -22):
                run(command([100, width], op) + move + end,
                    [(axis, 100, width)], diagnostics=2 if width == -22 else 0)
        stems = [(0, 10, 20), (1, 30, 40)]
        prefix = command([10, 20], 18)
        run(prefix + command([30, 40], 19) + b'\xc0' + move + line + b'\x13\x80' + end,
            stems, ['0 2 0 c0', '4 2 0 80'])
        run(prefix + command([30, 40], 20) + b'\xc0\x13\x80' + move + end,
            stems, ['0 2 1 c0', '0 2 0 80'])
        run(prefix + b'\x13\x81' + move + end, [(0, 10, 20)], ['0 1 0 81'], 8)
        run(b'\x13' + move + end, [], ['0 0 0'], 4)
        run(move + prefix + end, [(0, 10, 20)], diagnostics=1)
        run(command([30, 40], 3) + prefix + move + end,
            [(1, 30, 40), (0, 10, 20)], diagnostics=16)
        # Each stem operator starts its own delta sequence at the origin.
        run(prefix + command([5, 8], 1) + move + end, [(0, 10, 20), (0, 5, 8)])
        sub = prefix + b'\x0b'
        run(command([-107], 10) + b'\x13\x80' + move + end,
            [(0, 10, 20)], ['0 1 0 80'], local_subrs=[sub])
        run(prefix + b'\x13', ok=False)
        # 96 stems span the full twelve-byte mask; 97 is a structural failure.
        full = b''.join(command([0, 1] * 24, 18) for _ in range(4))
        expected = [(0, i, 1) for _ in range(4) for i in range(24)]
        run(full + b'\x13' + b'\xff' * 12 + move + end, expected,
            ['0 96 0' + ' ff' * 12])
        run(full + command([0, 1], 18) + move + end, ok=False)
    print(f'Type 2 hint capture: {checks} authored checks passed')


if __name__ == '__main__':
    main()
