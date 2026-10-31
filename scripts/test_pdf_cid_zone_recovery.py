#!/usr/bin/env python3
"""Check zero-zone CID compatibility without relaxing standalone validation."""

from pathlib import Path
import itertools
import ctypes
import subprocess
import sys
import tempfile
from pdf_cid_zone_fixture import font, asset, local_font_page, TWILIGHT
from test_pdf_embedded_fonts import fixture


def main():
    probe, allocator = [str(Path(x).resolve()) for x in sys.argv[1:3]]
    checks = allocations = references = 0
    with tempfile.TemporaryDirectory(prefix="cupid-cid-zones-") as temporary:
        path = Path(temporary) / "case.pdf"

        def render(data, aa=0, hint=1, reject=False):
            nonlocal checks
            path.write_bytes(data)
            result = subprocess.run(
                [probe, str(path), "0", str(aa), str(hint), "0", "0", str(aa)],
                capture_output=True,
                timeout=30,
            )
            assert result.returncode == (3 if reject else 0), result.stderr
            checks += 1
            return result.stdout

        def faults(data, aa=0, reject=False):
            nonlocal allocations
            path.write_bytes(data)
            result = subprocess.run(
                [allocator, str(path), str(int(reject)), str(aa), "1", "0", "0", str(aa)],
                capture_output=True,
                text=True,
                timeout=120,
            )
            assert result.returncode == 0, (result.stdout, result.stderr)
            allocations += int(result.stdout.split("checks")[0].split()[-1])

        for reordered in (False, True):
            for aa, hint, vertical, mode in itertools.product(
                (0, 1), (0, 1), (False, True), range(8)
            ):
                content = f".6 w 0 .2 .6 rg BT /F 18 Tf 25 60 Td {mode} Tr <0001000200040005> Tj ET"
                if mode >= 4:
                    content += " 1 .5 0 rg 0 0 120 90 re f"
                actual = fixture(
                    content, cid=True, vertical=vertical, font_data=font(0, reordered=reordered)
                )
                control = fixture(
                    content, cid=True, vertical=vertical, font_data=font(1, reordered=reordered)
                )
                assert render(actual, aa, hint) == render(control, aa, hint)
            for aa in (0, 1):
                faults(actual, aa)
        content = "BT /F 18 Tf 25 60 Td <0001> Tj ET"
        for bad in (font(0, 1), font(3), font(prep=TWILIGHT)):
            data = fixture(content, cid=True, font_data=bad)
            render(data, reject=True)
            faults(data, reject=True)
        # Ordinary simple-font hinting remains strict, while its unhinted path
        # does not depend on the unused instruction-profile metadata.
        simple = fixture("BT /F 18 Tf 25 60 Td (A) Tj ET", font_data=font())
        render(simple, reject=True)
        render(simple, hint=0)
        faults(simple, reject=True)
        for aa in (0, 1):
            assert render(local_font_page(), aa) == render(local_font_page(True), aa)
            faults(local_font_page(), aa)
        for aa, hint in itertools.product((0, 1), (0, 1)):
            assert render(asset(), aa, hint) == render(asset(True), aa, hint)
        if len(sys.argv) > 3:
            try:
                import freetype
                from pdf_tt_reference import outline_state
            except ImportError:
                print("CID zone references skipped: optional freetype-py unavailable")
            else:
                version = ctypes.c_uint(35)
                assert (
                    freetype.FT_Property_Set(
                        freetype.get_handle(),
                        b"truetype",
                        b"interpreter-version",
                        ctypes.byref(version),
                    )
                    == 0
                )
                glyph_probe = str(Path(sys.argv[3]).resolve())
                font_path = Path(temporary) / "owned.ttf"
                for reordered, aa, size in itertools.product(
                    (False, True), (0, 1), (8, 12, 16, 24, 48, 72)
                ):
                    data = font(reordered=reordered)
                    font_path.write_bytes(data)
                    face = freetype.Face.from_bytes(data)
                    face.set_pixel_sizes(size, size)
                    for glyph in (1, 2):
                        result = subprocess.run(
                            [
                                glyph_probe,
                                str(font_path),
                                "--native-subset-gray" if aa else "--native-subset",
                                str(size),
                                str(glyph),
                            ],
                            capture_output=True,
                            text=True,
                            timeout=30,
                        )
                        lines = result.stdout.splitlines()
                        assert result.returncode == 0 and lines[0].startswith("1 0 "), result.stdout
                        flags = freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT
                        if not aa:
                            flags |= freetype.FT_LOAD_TARGET_MONO
                        face.load_glyph(glyph, flags)
                        assert [
                            tuple(map(int, line.split())) for line in lines[1:]
                        ] == outline_state(face)
                        references += 1
    print(
        f"CID zone recovery: {checks} page checks; {allocations} allocation checks; {references} independent outlines"
    )


if __name__ == "__main__":
    main()
