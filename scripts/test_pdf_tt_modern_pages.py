#!/usr/bin/env python3
"""Exercise explicit modern TrueType fitting through complete PDF pages and the CLI."""

import itertools
from pathlib import Path
import subprocess
import sys
import tempfile
from pdf_tt_modern_fixture import pdf


def main():
    probe, allocator, cli = [str(Path(arg).resolve()) for arg in sys.argv[1:4]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix="cupid-modern-pages-") as temporary:
        path = Path(temporary) / "case.pdf"

        def render(content, aa=1, profile=2, **kwargs):
            nonlocal checks
            path.write_bytes(pdf(content, **kwargs))
            result = subprocess.run(
                [probe, str(path), "0", str(aa), str(profile)], capture_output=True, timeout=30
            )
            assert result.returncode == 0, (result.returncode, result.stderr, content)
            assert len(result.stdout) == 12 + 160 * 120 * 4
            checks += 1
            return result.stdout[12:]

        transforms = (
            "1 0 0 1 20 30",
            "1 .2 .1 1 20 30",
            "0 1 -1 0 65 20",
            "-1 0 0 1 90 30",
            ".8 0 0 1.3 20.125 30.375",
        )
        for size, matrix, mode in itertools.product((9, 18, 24), transforms, range(8)):
            content = f"BT /F {size} Tf {matrix} Tm {mode} Tr (AA) Tj ET"
            if mode >= 4:
                content += " 0 0 1 rg 0 0 120 90 re f"
            modern = render(content)
            control = render(content, profile=1, control=True)
            assert modern == control, (size, matrix, mode, "horizontal fitting applied")
            assert render(content, aa=0) == render(content, aa=0, profile=1), "monochrome changed"
            assert render(content, full=True) == render(
                content, profile=1, full=True
            ), "full fitting lost"
        # PDF advances, spacing and horizontal scale remain independent of fitted phantoms.
        for aa, scale in itertools.product((0, 1), (75, 100, 130)):
            prefix = f"BT /F 18 Tf {scale} Tz 2 Tc 20 30 Td "
            automatic = prefix + "[(A) 125 (A)] TJ ET"
            advance = (18 + 2 - 18 * 0.125) * scale / 100
            explicit = prefix + f"(A) Tj 1 0 0 1 {20 + advance} 30 Tm (A) Tj ET"
            assert render(automatic, aa=aa) == render(explicit, aa=aa), "PDF advance changed"
        # Identity-H/V descendants use the same profile without changing their PDF metrics.
        for vertical in (False, True):
            content = "BT /F 24 Tf 50 55 Td <00010001> Tj ET"
            assert render(content, cid=True, vertical=vertical) == render(
                content, profile=1, control=True, cid=True, vertical=vertical
            )
        for aa, full, mode in itertools.product((0, 1), (False, True), (0, 7)):
            content = f"BT /F 24 Tf 20 30 Td {mode} Tr (AA) Tj ET 0 0 1 rg 0 0 120 90 re f"
            render(content, aa=aa, full=full)
            result = subprocess.run(
                [allocator, str(path), "0", str(aa), "2"],
                capture_output=True,
                text=True,
                timeout=120,
            )
            assert result.returncode == 0, (result.stdout, result.stderr)
            allocations += int(result.stdout.split("checks")[0].split()[-1])
        # Nested content inherits the chosen profile while keeping local resources alive.
        from test_pdf_type3 import stream

        text = "BT /F 24 Tf 20 30 Td (AA) Tj ET"
        page = b"""<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90]
          /Resources << /Font << /F 5 0 R >> /XObject << /G 20 0 R >>
          /Pattern << /P 22 0 R >> /ExtGState << /Mask 23 0 R >> >> >>"""
        nested = {
            3: page,
            20: stream(
                text,
                "/Type /XObject /Subtype /Form /BBox [0 0 120 90] "
                "/Resources << /Font << /F 5 0 R >> >>",
            ),
            21: stream(
                "1 g " + text,
                "/Type /XObject /Subtype /Form /BBox [0 0 120 90] "
                "/Group << /S /Transparency /CS /DeviceGray /I true >> "
                "/Resources << /Font << /F 5 0 R >> >>",
            ),
            22: stream(
                text,
                "/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 "
                "/BBox [0 0 120 90] /XStep 120 /YStep 90 "
                "/Resources << /Font << /F 5 0 R >> >>",
            ),
            23: b"<< /Type /ExtGState /SMask << /S /Alpha /G 21 0 R >> >>",
        }
        for content in (
            "/G Do",
            "/Pattern cs /P scn 0 0 120 90 re f",
            "/Mask gs 1 0 0 rg 0 0 120 90 re f",
        ):
            actual = render(content, extra=nested)
            assert actual == render(content, profile=1, control=True, extra=nested)
            assert any(v != 255 for v in actual), "nested fixture empty"
            render(content, extra=nested)
            result = subprocess.run(
                [allocator, str(path), "0", "1", "2"], capture_output=True, text=True, timeout=120
            )
            assert result.returncode == 0, (result.stdout, result.stderr)
            allocations += int(result.stdout.split("checks")[0].split()[-1])
        from test_sixel import decode

        for aa in (0, 1):
            expected = render("1 0 0 rg BT /F 24 Tf 20 30 Td (AA) Tj ET", aa=aa)
            wanted = [
                tuple(expected[i + c] * 100 // 255 for c in range(3))
                for i in range(0, len(expected), 4)
            ]
            options = ["--pdf-tt-modern-hinting"] + (["--pdf-text-aa"] if aa else [])
            result = subprocess.run(
                [
                    cli,
                    "--sixel",
                    "--sixel-dither",
                    "none",
                    "--pdf-page",
                    "1",
                    *options,
                    str(path),
                    "160",
                    "120",
                ],
                capture_output=True,
                timeout=30,
            )
            assert result.returncode == 0, result.stderr
            transparent, dimensions, pixels = decode(result.stdout)
            assert not transparent and dimensions == (160, 120) and pixels == wanted
            checks += 1
        for flags in (
            ("--pdf-tt-hinting", "--pdf-tt-modern-hinting"),
            ("--pdf-tt-modern-hinting", "--pdf-tt-hinting"),
        ):
            result = subprocess.run([cli, *flags, str(path)], capture_output=True, timeout=30)
            assert result.returncode != 0 and not result.stdout
            checks += 1
    print(f"Modern TrueType pages: {checks} checks, {allocations} allocation failures")


if __name__ == "__main__":
    main()
