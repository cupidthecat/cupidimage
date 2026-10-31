#!/usr/bin/env python3
"""Check standard Type 1 flex collapse and literal retained-curve adjustments."""

from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_type1 import fixture
from test_pdf_type1_charstrings import WIDTH, command
from test_pdf_type1_render import fixture as page_fixture

SUBRS = [
    command([3, 0], (12, 16)) + bytes([12, 17, 12, 17, 12, 33, 11]),
    command([0, 1], (12, 16)) + b"\x0b",
    command([0, 2], (12, 16)) + b"\x0b",
]
MOVE = command([100, -10], 21)
FINISH = command([0, -100], 5) + command([-100, 0], 5) + b"\x09\x0e"


def flex_program():
    code = WIDTH + MOVE + command([1], 10)
    for x, y in ((50, 0), (-35, 0), (10, 10), (25, 0), (25, 0), (10, -10), (15, 0)):
        code += command([x, y], 21) + command([2], 10)
    return code + command([50, 200, -10, 0], 10) + FINISH


def control_program(line, join=0):
    body = (
        command([100, 0], 5)
        if line
        else command([15, 0, 10, join + 10, 25, 0], 8) + command([25, 0, 10, -10 - join, 15, 0], 8)
    )
    return WIDTH + MOVE + body + FINISH


def main():
    probe, trace_probe = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix="cupid-type1-flex-") as temporary:
        path = Path(temporary) / "font.pfa"

        def build(code, paint):
            data, l1, l2 = fixture(
                glyphs=[(".notdef", WIDTH + b"\x0e"), ("A", code)], subrs=SUBRS, paint_type=paint
            )
            path.write_bytes(data)
            return l1, l2

        def outline(code, ppem, paint=0):
            l1, l2 = build(code, paint)
            result = subprocess.run(
                [probe, str(path), str(l1), str(l2), str(ppem), str(ppem)],
                capture_output=True,
                text=True,
                timeout=20,
            )
            assert result.returncode == 0, (result.stdout, result.stderr)
            assert "glyph A 1 " in result.stdout, result.stdout
            return result.stdout

        for paint in (0, 2):
            for ppem in (8, 16, 32, 125, 500):
                line = ppem < 40
                join = 2 if ppem == 125 else 0
                assert outline(flex_program(), ppem, paint) == outline(
                    control_program(line, join), ppem, paint
                ), (ppem, paint)
                checks += 1
        l1, l2 = build(flex_program(), 0)
        out = subprocess.check_output(
            [trace_probe, str(path), str(l1), str(l2), "--hint-outline"], text=True
        )
        assert "hint-event 1 4 1 0 50 200 -10 150 -10 0" in out, out
        checks += 1
        fraction = flex_program().replace(
            command([50, 200, -10, 0], 10), command([125, 8], (12, 12)) + command([200, -10, 0], 10)
        )
        l1, l2 = build(fraction, 0)
        out = subprocess.check_output(
            [trace_probe, str(path), str(l1), str(l2), "--hint-outline"], text=True
        )
        assert "hint-event 1 4 1 0 15.625 200 -10 150 -10 0" in out, out
        checks += 1
        if len(sys.argv) > 3:
            pages, allocator = [str(Path(p).resolve()) for p in sys.argv[3:5]]
            pdf = Path(temporary) / "case.pdf"

            def render(code, matrix, mode, aa, hint, paint=0, extras=(), char="A", size=12):
                content = f"BT /F {size} Tf {mode} Tr {matrix} 40.25 40.25 Tm ({char}) Tj ET"
                if mode >= 4:
                    content += " 1 0 0 rg 0 0 120 90 re f"
                pdf.write_bytes(
                    page_fixture(
                        content,
                        glyphs=[(".notdef", WIDTH + b"\x0e"), ("A", code), *extras],
                        metrics="/FirstChar 65 /LastChar 67 /Widths [600 600 600]",
                        subrs=SUBRS,
                        paint_type=paint,
                    )
                )
                result = subprocess.run(
                    [pages, str(pdf), "0", str(aa), "0", "0", str(hint)],
                    capture_output=True,
                    timeout=30,
                )
                assert result.returncode == 0, (matrix, mode, result.stderr)
                assert len(result.stdout) == 12 + 160 * 120 * 4
                if char in ("B", "C"):
                    assert any(
                        value != 255 for value in result.stdout[12:]
                    ), "empty composite comparison"
                return result.stdout

            for matrix in (
                "1 0 0 1",
                "0 1 -1 0",
                "-1 0 0 1",
                "2 0 0 .5",
                "1 .25 0 1",
                "1 .25 -.125 1",
            ):
                line = matrix != "1 .25 -.125 1"
                for aa in (0, 1):
                    for mode in range(8):
                        actual = render(flex_program(), matrix, mode, aa, 1)
                        assert actual == render(control_program(line), matrix, mode, aa, 1), (
                            matrix,
                            mode,
                            aa,
                            "selected flex path",
                        )
                        assert actual == render(
                            flex_program(), matrix, mode, aa, 1, 2
                        ), "embedded PaintType changed PDF glyphs"
                        assert render(flex_program(), matrix, mode, aa, 0) == render(
                            control_program(False), matrix, mode, aa, 0
                        ), "unhinted Type 1 flex changed"
                        checks += 3
            # Compare a flex-bearing seac accent with the same pre-fit geometry
            # written as a second contour, preserving component pixel phase.
            composite = WIDTH + command([0, 150, 25, 65, 65], (12, 6))
            first = flex_program()[:-1]
            second = command([150, 125], 21) + command([1], 10)
            for x, y in ((50, 0), (-35, 0), (10, 10), (25, 0), (25, 0), (10, -10), (15, 0)):
                second += command([x, y], 21) + command([2], 10)
            second += command([50, 350, 15, 0], 10) + FINISH
            extras = [("B", composite), ("C", first + second)]
            for size in (12, 93.75):
                for matrix in ("1 0 0 1", "0 1 -1 0", "1 .25 -.125 1"):
                    for aa in (0, 1):
                        for mode in (0, 2, 7):
                            assert render(
                                flex_program(),
                                matrix,
                                mode,
                                aa,
                                1,
                                extras=extras,
                                char="B",
                                size=size,
                            ) == render(
                                flex_program(),
                                matrix,
                                mode,
                                aa,
                                1,
                                extras=extras,
                                char="C",
                                size=size,
                            ), (
                                size,
                                matrix,
                                mode,
                                aa,
                                "flex component origin",
                            )
                            checks += 1
            render(flex_program(), "1 0 0 1", 2, 1, 1, extras=extras, char="B")
            result = subprocess.run(
                [allocator, str(pdf), "0", "1", "0", "0", "1"],
                capture_output=True,
                text=True,
                timeout=120,
            )
            assert result.returncode == 0, (result.stdout, result.stderr)
            allocations += int(result.stdout.split("checks")[0].split()[-1])
            for matrix in ("1 0 0 1", "0 1 -1 0", "1 .25 -.125 1"):
                render(flex_program(), matrix, 2, 1, 1)
                result = subprocess.run(
                    [allocator, str(pdf), "0", "1", "0", "0", "1"],
                    capture_output=True,
                    text=True,
                    timeout=120,
                )
                assert result.returncode == 0, (result.stdout, result.stderr)
                allocations += int(result.stdout.split("checks")[0].split()[-1])
    print(
        f"Type 1 flex: {checks} literal outline/trace/page checks; {allocations} allocation checks"
    )


if __name__ == "__main__":
    main()
