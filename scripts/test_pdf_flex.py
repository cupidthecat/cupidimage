#!/usr/bin/env python3
"""Compare authored Type 2 flex programs with their specified line/curve controls."""

from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_cff import fixture
from test_pdf_type2 import command
from test_pdf_cff_render import fixture as page_fixture
import math


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix="cupid-flex-") as temporary:
        path = Path(temporary) / "font.cff"

        def outline(program, ppem):
            path.write_bytes(fixture(glyphs=1, charset=b"\0", programs=[program]))
            result = subprocess.run(
                [probe, str(path), str(ppem), str(ppem)], capture_output=True, timeout=20
            )
            assert result.returncode == 0, result.stderr
            return result.stdout

        move = command([0, 100], 21)
        finish = command([0, -100, -120, 0, 0, 100], 5) + b"\x0e"
        deltas = [20, 0, 20, 12, 20, 0, 20, 0, 20, -12, 20, 0]
        cases = [
            (34, [20, 20, 12, 20, 20, 20, 20]),
            (35, deltas + [50]),
            (36, [20, 0, 20, 12, 20, 20, 20, -12, 20]),
            (37, deltas[:10] + [20]),
        ]
        cases += [(35, deltas + [depth]) for depth in (0, 25, 100, -1)]
        for op, args in cases:
            for ppem in (8, 16, 32, 64, 128):
                actual = outline(move + command(args, (12, op)) + finish, ppem)
                expected = outline(
                    move
                    + (
                        command([120, 0], 5)
                        if 12 * ppem / 1000 < (args[-1] if op == 35 else 50) / 100
                        else command(deltas, 8)
                    )
                    + finish,
                    ppem,
                )
                assert actual == expected, (op, ppem, actual, expected)
                checks += 1
        if len(sys.argv) > 2:
            page_probe, allocator = [str(Path(p).resolve()) for p in sys.argv[2:4]]
            page = Path(temporary) / "case.pdf"

            def render(code, size, matrix, aa, hint, mode):
                font = fixture(programs=[b"\x0e", b"\x0e", code, b"\x0e"])
                content = f"BT /F {size} Tf {mode} Tr {matrix} 40 40 Tm (A) Tj ET"
                if mode >= 4:
                    content += " 1 0 0 rg 0 0 120 90 re f"
                page.write_bytes(page_fixture(content, font=font))
                result = subprocess.run(
                    [page_probe, str(page), "0", str(aa), "0", str(hint)],
                    capture_output=True,
                    timeout=30,
                )
                assert result.returncode == 0, (matrix, result.stderr)
                return result.stdout

            flex = move + command(deltas + [50], (12, 35)) + finish
            line = move + command([120, 0], 5) + finish
            curves = move + command(deltas, 8) + finish
            for size in (6, 12, 24, 48):
                for matrix in (
                    "1 0 0 1",
                    "0 1 -1 0",
                    "-1 0 0 1",
                    "2 0 0 .5",
                    "1 1 0 1",
                    "1 .25 .5 1",
                ):
                    a, b, c, d = map(float, matrix.split())
                    # Depth of the transformed join relative to the transformed chord.
                    depth = 12 * size * (96 / 72) / 1000 * abs(a * d - b * c) / math.hypot(a, b)
                    control = line if depth < 0.5 else curves
                    for aa in (0, 1):
                        for mode in (0, 1, 2, 4, 5, 6, 7):
                            assert render(flex, size, matrix, aa, 1, mode) == render(
                                control, size, matrix, aa, 1, mode
                            ), (size, matrix, aa, mode, "device flex choice")
                            assert render(flex, size, matrix, aa, 0, mode) == render(
                                curves, size, matrix, aa, 0, mode
                            ), "unhinted flex geometry changed"
                            checks += 2
            for op, args in cases:
                render(move + command(args, (12, op)) + finish, 12, "1 0 0 1", 1, 1, 2)
                result = subprocess.run(
                    [allocator, str(page), "0", "1", "0", "1"],
                    capture_output=True,
                    text=True,
                    timeout=120,
                )
                assert result.returncode == 0, (result.stdout, result.stderr)
                allocations += int(result.stdout.split("checks")[0].split()[-1])
    print(
        f"Type 2 flex: {checks} specified outline/page comparisons; {allocations} allocation checks"
    )


if __name__ == "__main__":
    main()
