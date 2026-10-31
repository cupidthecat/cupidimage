#!/usr/bin/env python3
"""Report Type 1 flex policy differences separately from matching device outlines."""

from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_type1 import fixture
from test_pdf_type1_charstrings import WIDTH
from test_pdf_type1_flex import flex_program, control_program, SUBRS


def main():
    try:
        import freetype
    except ImportError:
        print("Type 1 flex references skipped: optional freetype-py unavailable")
        return
    probe = str(Path(sys.argv[1]).resolve())
    matches = differences = 0
    with tempfile.TemporaryDirectory(prefix="cupid-type1-flex-reference-") as temporary:
        path = Path(temporary) / "font.pfa"

        def reference(code, ppem):
            data, l1, l2 = fixture(glyphs=[(".notdef", WIDTH + b"\x0e"), ("A", code)], subrs=SUBRS)
            path.write_bytes(data)
            face = freetype.Face(str(path))
            face.set_pixel_sizes(ppem, ppem)
            face.load_glyph(1, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT)
            output = []
            face.glyph.outline.decompose(
                None,
                move_to=lambda p, _: output.append((0, p.x, p.y)),
                line_to=lambda p, _: output.append((1, p.x, p.y)),
                cubic_to=lambda a, b, c, _: output.append((2, a.x, a.y, b.x, b.y, c.x, c.y)),
            )
            if output:
                output.append((3,))
            return output, l1, l2

        for ppem in (8, 16, 32, 64, 125, 250, 500):
            expected, l1, l2 = reference(flex_program(), ppem)
            lines = subprocess.check_output(
                [probe, str(path), str(l1), str(l2), str(ppem), str(ppem)], text=True
            ).splitlines()
            at = next(i for i, line in enumerate(lines) if line.startswith("glyph A "))
            assert lines[at].split()[2] == "1"
            actual = [tuple(map(int, line.split()[1:])) for line in lines[at + 1 :]]
            if actual == expected:
                matches += 1
            else:
                control, _, _ = reference(control_program(False), ppem)
                assert expected == control, (
                    ppem,
                    "unexplained independent difference",
                    actual,
                    expected,
                )
                differences += 1
                print(f"Type 1 flex difference: {ppem} ppem, reference retains unadjusted curves")
    print(
        f"Type 1 flex references: {matches} exact outlines; {differences} unadjusted-curve differences; engine {freetype.version()}"
    )


if __name__ == "__main__":
    main()
