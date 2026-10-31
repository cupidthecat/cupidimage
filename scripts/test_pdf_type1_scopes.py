#!/usr/bin/env python3
"""Check Type 1 dictionary ownership and inherited literal syntax bindings."""

import argparse
from pathlib import Path
import subprocess
import tempfile
from test_pdf_type1_page_hinting import pdf
from pdf_type1_scopes_fixture import POSITIVE, NEGATIVE, program


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font_probe", type=Path)
    parser.add_argument("page_probe", type=Path)
    parser.add_argument("allocator", type=Path)
    parser.add_argument("--reference", action="store_true")
    parser.add_argument("--font-allocator", type=Path)
    args = parser.parse_args()
    structures = pages = allocations = references = font_allocations = 0
    with tempfile.TemporaryDirectory(prefix="cupid-type1-scopes-") as temporary:
        root = Path(temporary)
        font_path, page_path = root / "font.bin", root / "page.pdf"

        def inspect(parts, mode):
            data, clear, encrypted = parts
            font_path.write_bytes(data)
            return subprocess.run(
                [str(args.font_probe.resolve()), str(font_path), str(clear), str(encrypted), mode],
                capture_output=True,
                timeout=15,
            )

        def font_ownership(parts, expected):
            nonlocal font_allocations
            if not args.font_allocator:
                return
            data, clear, encrypted = parts
            font_path.write_bytes(data)
            result = subprocess.run(
                [
                    str(args.font_allocator.resolve()),
                    str(font_path),
                    str(clear),
                    str(encrypted),
                    str(int(expected)),
                ],
                capture_output=True,
                timeout=20,
            )
            assert result.returncode == 0, result.stderr
            font_allocations += int(result.stdout)

        def render(parts, mode, aa, hint):
            content = (
                f"BT /F 30 Tf {mode} Tr 20 30 Td (AAA) Tj ET 0.2 0.4 0.8 rg 0 0 120 90 re f"
                if mode == 7
                else f"BT /F 30 Tf {mode} Tr 20 30 Td (AAA) Tj ET"
            )
            page_path.write_bytes(
                pdf(
                    content,
                    program=parts,
                    encoding="/WinAnsiEncoding",
                    metrics="/FirstChar 65 /LastChar 65 /Widths [600]",
                )
            )
            run = subprocess.run(
                [str(args.page_probe.resolve()), str(page_path), "0", str(aa), "0", "0", str(hint)],
                capture_output=True,
                timeout=20,
            )
            assert run.returncode == 0, (mode, aa, hint, run.stderr)
            return run.stdout

        for outer in (False, True):
            for len_iv in (-1, 0, 4):
                control = program(len_iv=len_iv, outer=outer)
                outline = inspect(control, "--hint-outline")
                hints = inspect(control, "--hints")
                assert outline.returncode == hints.returncode == 0
                controls = {
                    (mode, aa): render(control, mode, aa, aa) for mode in (0, 2, 7) for aa in (0, 1)
                }
                for name in POSITIVE:
                    parts = program(name, len_iv, outer)
                    actual = inspect(parts, "--hint-outline")
                    actual_hints = inspect(parts, "--hints")
                    assert actual.returncode == 0 and actual.stdout == outline.stdout, (
                        name,
                        len_iv,
                        outer,
                        actual.stderr,
                    )
                    assert actual_hints.returncode == 0 and actual_hints.stdout == hints.stdout, (
                        name,
                        "hint metadata differs",
                    )
                    structures += 1
                    font_ownership(parts, True)
                    for (mode, aa), expected in controls.items():
                        assert render(parts, mode, aa, aa) == expected, (
                            name,
                            len_iv,
                            outer,
                            mode,
                            aa,
                        )
                        pages += 1
                    if (
                        outer
                        and len_iv == 4
                        and name
                        in (
                            "nested-leniv",
                            "nested-binary-subrs",
                            "nested-charstrings",
                            "global-reader",
                            "active-clear-reader",
                            "active-clear-definer",
                            "userdict-reader",
                            "userdict-begin-definer",
                            "currentdict-private",
                        )
                    ):
                        result = subprocess.run(
                            [
                                str(args.allocator.resolve()),
                                str(page_path),
                                "0",
                                "1",
                                "0",
                                "0",
                                "1",
                            ],
                            capture_output=True,
                            timeout=120,
                        )
                        assert result.returncode == 0, (name, result.stderr)
                        allocations += int(result.stdout.decode().split(": ")[1].split()[0])
                for name in NEGATIVE:
                    assert (
                        inspect(program(name, len_iv, outer), "--hint-outline").returncode != 0
                    ), (name, len_iv, outer)
                    structures += 1
                    font_ownership(program(name, len_iv, outer), False)
                parts = program("readonly-context-reader", len_iv, outer)
                actual = inspect(parts, "--hint-outline")
                assert (
                    (actual.returncode != 0)
                    if outer
                    else (actual.returncode == 0 and actual.stdout == outline.stdout)
                )
                structures += 1
                font_ownership(parts, not outer)
                if not outer:
                    for (mode, aa), expected in controls.items():
                        assert render(parts, mode, aa, aa) == expected
                        pages += 1
        from generate_pdf_type1_scopes_fixture import build as sample_page

        for hint in (0, 1):
            results = []
            for controls_only in (False, True):
                page_path.write_bytes(sample_page(controls=controls_only))
                result = subprocess.run(
                    [
                        str(args.page_probe.resolve()),
                        str(page_path),
                        "0",
                        str(hint),
                        "0",
                        "0",
                        str(hint),
                    ],
                    capture_output=True,
                    timeout=20,
                )
                assert result.returncode == 0, result.stderr
                results.append(result.stdout)
            assert results[0] == results[1], "scope sample differs from its literal control"
            pages += 1
        page_path.write_bytes(sample_page())
        result = subprocess.run(
            [str(args.allocator.resolve()), str(page_path), "0", "1", "0", "0", "1"],
            capture_output=True,
            timeout=180,
        )
        assert result.returncode == 0, result.stderr
        allocations += int(result.stdout.decode().split(": ")[1].split()[0])
        if args.reference:
            from PIL import Image

            driver = root / "draw.ps"
            driver.write_text(
                "clear << /PageSize [120 90] >> setpagedevice /CupidShapes findfont 30 scalefont setfont 20 30 moveto (AAA) show showpage\n"
            )
            for outer in (False, True):
                for len_iv in (-1, 0, 4):
                    expected = None
                    for name in (
                        ("control",) + POSITIVE + (() if outer else ("readonly-context-reader",))
                    ):
                        data, clear, encrypted = program(name, len_iv, outer)
                        font_path.write_bytes(
                            data + (b"\n" + b"0" * 512 + b"\ncleartomark\n" if encrypted else b"\n")
                        )
                        png = root / "reference.png"
                        result = subprocess.run(
                            [
                                "gs",
                                "-q",
                                "-dSAFER",
                                "-dBATCH",
                                "-dNOPAUSE",
                                "-sDEVICE=png16m",
                                "-r96",
                                "-dTextAlphaBits=1",
                                "-dGraphicsAlphaBits=1",
                                f"-sOutputFile={png}",
                                str(font_path),
                                str(driver),
                            ],
                            capture_output=True,
                            timeout=20,
                        )
                        assert result.returncode == 0, (name, result.stdout, result.stderr)
                        with Image.open(png) as image:
                            pixels = image.convert("RGB").tobytes()
                        if expected is None:
                            expected = pixels
                            assert any(v != 255 for v in pixels)
                        else:
                            assert pixels == expected, (name, "direct PostScript execution differs")
                            references += 1
    print(
        f"Type 1 scopes: {structures} font cases, {pages} page controls, {allocations} page allocation checks, {font_allocations} parser allocation checks, {references} independent program pairs"
    )


if __name__ == "__main__":
    main()
