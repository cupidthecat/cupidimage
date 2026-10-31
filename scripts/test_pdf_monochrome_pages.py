#!/usr/bin/env python3
"""Verify scan policy plumbing through PDF placement and painting."""

import base64
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_page_hinting import fixture

DATA = json.loads(Path("tests/fixtures/pdf-monochrome-font.json").read_text())
FONT = base64.b64decode(DATA["font"])
CONTROL = base64.b64decode(DATA["control"])
METRICS = "/FirstChar 65 /LastChar 82 /Widths [" + "1000 " * 18 + "]"


def main():
    probe, allocator = [str(Path(value).resolve()) for value in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix="cupid-monochrome-") as temporary:
        path = Path(temporary) / "case.pdf"

        def render(
            char,
            matrix="1 0 0 1",
            aa=0,
            hint=1,
            control=False,
            paint=0,
            origin="20.25 30",
            prefix="",
            suffix="",
            extra=None,
            content_override=None,
        ):
            nonlocal checks
            content = (
                f"{prefix} BT /F 12 Tf {paint} Tr {matrix} {origin} Tm ({char}) Tj ET {suffix}"
            )
            if content_override is not None:
                content = content_override
            path.write_bytes(
                fixture(
                    content,
                    font_data=CONTROL if control else FONT,
                    font_name="CupidImageScanPolicies",
                    metrics=METRICS,
                    extra=extra,
                )
            )
            result = subprocess.run(
                [probe, str(path), "0", str(aa), str(hint)], capture_output=True, timeout=30
            )
            assert result.returncode == 0, (char, matrix, result.stderr)
            checks += 1
            return result.stdout[12:]

        for matrix in ("1 0 0 1", "0 1 -1 0", "2 0 0 1", "1 .25 0 1", "-1 0 0 -1", "-1 0 0 1"):
            for char in "ABCDEFGHIJKLMNQR":
                for aa, hint in ((0, 0), (1, 0), (1, 1)):
                    assert render(char, matrix, aa, hint) == render(char, matrix, aa, hint, True), (
                        char,
                        matrix,
                        "changed non-monochrome geometry",
                    )
            # With no point-moving instructions, all unconditional policies share geometry.
            assert render("A", matrix) == render("N", matrix), (
                "conditional state was not preserved",
                matrix,
            )
            assert (
                render("C", matrix)
                == render("D", matrix)
                == render("G", matrix)
                == render("H", matrix)
                == render("I", matrix)
            ), ("dropout disabled modes differ", matrix)
            rotated = matrix not in ("1 0 0 1", "2 0 0 1", "-1 0 0 1")
            stretched = matrix in ("2 0 0 1", "1 .25 0 1")
            for char, enabled in (
                ("J", rotated),
                ("K", stretched),
                ("L", rotated),
                ("M", stretched),
            ):
                assert render(char, matrix) == render("A" if enabled else "I", matrix), (
                    char,
                    matrix,
                    "transform condition",
                )
        # The narrow outline spans x=29.5625..29.8125 at this origin. No pixel
        # center lies inside it. Dropout modes select column 29; excluding stubs
        # suppresses the first and last rows of the eight-pixel contour.
        for char in "ABCDEFGHIJKLMNQR":
            expected = bytearray(b"\xff" * (160 * 120 * 4))
            columns = (29, 37) if char == "Q" else (37,) if char == "R" else (29,)
            rows = range(73, 79) if char in "BF" else range(72, 80) if char in "AENQR" else ()
            for y in rows:
                for x in columns:
                    at = (y * 160 + x) * 4
                    expected[at : at + 3] = b"\x00\x00\x00"
            assert render(char) == expected, (char, "analytic narrow stem/composite bitmap")
        assert render("A") != render("C"), "thin stems did not receive dropout pixels"
        for char in "ABCDEFGH":
            # Stroke-only and clip-only outlines must not use a dropout-expanded silhouette.
            for paint in (1, 7):
                suffix = "0 0 120 90 re f" if paint == 7 else ""
                assert render(char, paint=paint, suffix=suffix) == render(
                    char, control=True, paint=paint, suffix=suffix
                )
            for prefix in ("", "/Half gs", "q 19 25 4 20 re W n"):
                render(char, prefix=prefix, suffix="Q" if prefix.startswith("q") else "")
                result = subprocess.run(
                    [allocator, str(path), "0", "0", "1"],
                    capture_output=True,
                    text=True,
                    timeout=120,
                )
                assert result.returncode == 0, (char, result.stdout, result.stderr)
                allocations += int(result.stdout.split("checks")[0].split()[-1])
        # Combined fill/stroke is an implicit knockout operation: overlap is
        # painted once, including dropout pixels outside the geometric fill.
        for char in "ABCEQR":
            for opacity in ("", "/Half gs"):
                prefix = opacity + " 1 w"
                fill = render(char, prefix=prefix)
                stroke = render(char, prefix=prefix, paint=1)
                assert render(char, prefix=prefix, paint=2) == bytes(
                    min(a, b) for a, b in zip(fill, stroke)
                ), (char, "combined paint overlap")

        from test_pdf_forms import form
        from test_pdf_type3 import stream

        resources = "/Font << /F 5 0 R >> /XObject << /G 20 0 R >> /Pattern << /P 21 0 R >>"
        page = (
            "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] "
            "/Resources << " + resources + " >> >>"
        ).encode()
        text = "BT /F 12 Tf 1 0 0 1 20.25 30 Tm (A) Tj ET"
        objects = {
            3: page,
            20: form(text, "/BBox [0 0 120 90] /Group << /S /Transparency /I true >>"),
            21: stream(
                "1 0 0 rg 0 0 4 4 re f",
                "/Type /Pattern /PatternType 1 /PaintType 1 "
                "/TilingType 1 /BBox [0 0 4 4] /XStep 4 /YStep 4 /Resources << >>",
            ),
        }
        assert render("A", extra=objects, content_override="/G Do") == render(
            "A"
        ), "Form/group dropout fill"
        assert render("A", extra=objects, prefix="/Pattern cs /P scn") == render(
            "A", prefix="1 0 0 rg"
        ), "pattern dropout fill"
        # Repeat transforms in one page to exercise prepared-state cache keys,
        # rather than merely checking each transform in a fresh document.
        programs = []
        singles = []
        for matrix, origin in (
            ("1 0 0 1", "20.25 30"),
            ("0 1 -1 0", "60.25 30"),
            ("2 0 0 1", "90.25 30"),
            ("1 0 0 1", "20.25 60"),
        ):
            programs.append(f"BT /F 12 Tf {matrix} {origin} Tm (J) Tj ET")
            singles.append(render("J", matrix=matrix, origin=origin))
        expected = bytes(min(values) for values in zip(*singles))
        assert (
            render("J", content_override=" ".join(programs)) == expected
        ), "scan cache transform switch"
        assert (
            render("J", content_override=" ".join(reversed(programs))) == expected
        ), "scan cache reverse switch"
    print(f"Monochrome pages: {checks} checks; {allocations} allocation/ownership checks")


if __name__ == "__main__":
    main()
