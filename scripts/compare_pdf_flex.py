#!/usr/bin/env python3
"""Report authored flex comparisons without treating retained-curve differences as matches."""

from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_cff import fixture
from test_pdf_type2 import command


def main():
    try:
        import freetype
    except ImportError:
        print("Flex references skipped: optional freetype-py unavailable")
        return
    probe = str(Path(sys.argv[1]).resolve())
    matches = retained = 0
    move = command([0, 100], 21)
    deltas = [20, 0, 20, 12, 20, 0, 20, 0, 20, -12, 20, 0]
    finish = command([0, -100, -120, 0, 0, 100], 5) + b"\x0e"
    cases = [
        (34, [20, 20, 12, 20, 20, 20, 20]),
        (35, deltas + [50]),
        (36, [20, 0, 20, 12, 20, 20, 20, -12, 20]),
        (37, deltas[:10] + [20]),
    ]
    with tempfile.TemporaryDirectory(prefix="cupid-flex-reference-") as temporary:
        path = Path(temporary) / "font.cff"

        def reference(code, ppem):
            path.write_bytes(fixture(glyphs=1, charset=b"\0", programs=[code]))
            face = freetype.Face(str(path))
            face.set_pixel_sizes(ppem, ppem)
            face.load_glyph(0, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT)
            output = []
            face.glyph.outline.decompose(
                None,
                move_to=lambda p, _: output.append((0, p.x, p.y)),
                line_to=lambda p, _: output.append((1, p.x, p.y)),
                cubic_to=lambda a, b, c, _: output.append((2, a.x, a.y, b.x, b.y, c.x, c.y)),
            )
            if output:
                output.append((3,))
            return output

        for op, args in cases:
            for ppem in (8, 16, 32, 64, 128):
                expected = reference(move + command(args, (12, op)) + finish, ppem)
                lines = subprocess.check_output(
                    [probe, str(path), str(ppem), str(ppem)], text=True
                ).splitlines()
                assert lines[0].split()[2] == "1", lines
                native = [
                    tuple(map(int, line.split()[1:])) if not line.startswith("segment 3") else (3,)
                    for line in lines[1:]
                ]
                if native == expected:
                    matches += 1
                else:
                    curves = reference(move + command(deltas, 8) + finish, ppem)
                    assert expected == curves, (
                        op,
                        ppem,
                        "unexplained reference difference",
                        native,
                        expected,
                    )
                    retained += 1
                    print(
                        f"Flex difference: opcode {op}, {ppem} ppem, reference retains the two curves"
                    )
    print(
        f"Flex references: {matches} exact outline matches; {retained} retained-curve differences; engine {freetype.version()}"
    )


if __name__ == "__main__":
    main()
