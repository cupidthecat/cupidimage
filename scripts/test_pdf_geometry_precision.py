#!/usr/bin/env python3
"""Check exact equivalent PDF transforms through text and shared graphics contexts."""

import argparse
import itertools
from pathlib import Path
import struct
import subprocess
import tempfile
from test_pdf_embedded_fonts import fixture as truetype
from test_pdf_cff_render import fixture as cff
from test_pdf_type1_render import fixture as type1
from test_pdf_type3 import fixture as type3, stream
from test_pdf_pages import document

BIG = 16777216
CHAIN = f"1 0 0 1 {BIG} 0 cm 1 0 0 1 1 0 cm 1 0 0 1 {-BIG} 0 cm "
DIRECT = "1 0 0 1 1 0 cm "
TEXT = "BT /F 24 Tf 20.1875 30.1875 Td (AA) Tj ET"
PAGE = b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 120 90] /Contents 4 0 R /Resources "
RESOURCES = (
    b"<< /Font << /F 5 0 R >> /XObject << /G 20 0 R /Im 30 0 R >> "
    b"/Pattern << /P 21 0 R >> /ExtGState << /Mask 22 0 R >> >> >>"
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path)
    parser.add_argument("allocator", type=Path)
    parser.add_argument("--reference", action="store_true")
    args = parser.parse_args()
    probe, allocator = str(args.probe.resolve()), str(args.allocator.resolve())
    checks = allocations = references = reference_differences = rejections = 0
    with tempfile.TemporaryDirectory(prefix="cupid-geometry-") as temporary:
        root = Path(temporary)
        path = root / "case.pdf"

        def render(data, options=(0, 0, 0, 0, 0)):
            path.write_bytes(data)
            result = subprocess.run(
                [probe, str(path), "0", *map(str, options)], capture_output=True, timeout=30
            )
            assert result.returncode == 0, result.stderr
            width, height, pages = struct.unpack("=III", result.stdout[:12])
            assert pages == 1 and len(result.stdout) == 12 + width * height * 4
            return result.stdout

        def pair(label, actual, expected, options=(0, 0, 0, 0, 0), failure=False, reference=False):
            nonlocal checks, allocations, references, reference_differences
            a, b = render(actual, options), render(expected, options)
            assert a == b, (label, sum(x != y for x, y in zip(a, b)))
            checks += 1
            if failure:
                path.write_bytes(actual)
                result = subprocess.run(
                    [allocator, str(path), "0", *map(str, options)],
                    capture_output=True,
                    text=True,
                    timeout=120,
                )
                assert result.returncode == 0, (result.stdout, result.stderr)
                allocations += int(result.stdout.split("checks")[0].split()[-1])
            if reference and args.reference:
                images = []
                for index, data in enumerate((actual, expected)):
                    source = root / f"reference-{index}.pdf"
                    prefix = root / f"reference-{index}"
                    source.write_bytes(data)
                    result = subprocess.run(
                        ["pdftoppm", "-r", "96", "-singlefile", str(source), str(prefix)],
                        capture_output=True,
                        timeout=30,
                    )
                    assert result.returncode == 0, result.stderr
                    images.append(prefix.with_suffix(".ppm").read_bytes())
                if images[0] == images[1]:
                    references += 1
                elif reference == "phase-diagnostic":
                    reference_differences += 1
                else:
                    raise AssertionError((label, "independent equivalent pages differ"))

        chains = [
            (CHAIN, DIRECT),
            (f"1 0 0 1 0 {BIG} cm 1 0 0 1 0 .125 cm 1 0 0 1 0 {-BIG} cm ", "1 0 0 1 0 .125 cm "),
            ("1 .25 -.125 1 0 0 cm " + CHAIN, "1 .25 -.125 1 0 0 cm " + DIRECT),
            ("-1 0 0 1 110 0 cm " + CHAIN, "-1 0 0 1 110 0 cm " + DIRECT),
            ("0 1 -1 0 100 10 cm " + CHAIN, "0 1 -1 0 100 10 cm " + DIRECT),
        ]
        for name, maker in (
            ("TrueType", truetype),
            ("CFF", cff),
            ("Type1", type1),
            ("Type3", type3),
        ):
            for (chain, direct), mode, aa in itertools.product(chains, range(8), (0, 1)):
                text = f"BT /F 24 Tf {mode} Tr 20.1875 30.1875 Td (AA) Tj ET"
                if mode >= 4:
                    text += " 1 0 0 rg 0 0 120 90 re f"
                suffix = " Q 0 0 1 rg BT /F 12 Tf 80 70 Td (A) Tj ET"
                options = (aa, 2, 1, 1, aa)
                pair(
                    (name, mode, aa, chain),
                    maker("q " + chain + text + suffix),
                    maker("q " + direct + text + suffix),
                    options,
                    failure=mode == 7 and chain == CHAIN,
                    reference=(
                        "phase-diagnostic"
                        if name == "TrueType" and mode == 0 and aa == 0 and chain == CHAIN
                        else False
                    ),
                )
            form = "/Type /XObject /Subtype /Form /BBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> >> /Matrix "
            objects = {3: PAGE + RESOURCES, 20: stream(TEXT, form + f"[1 0 0 1 {BIG+.125:.3f} 0]")}
            control = {3: PAGE + RESOURCES, 20: stream(TEXT, form + "[1 0 0 1 .125 0]")}
            pair(
                name + " Form matrix",
                maker(f"1 0 0 1 {-BIG} 0 cm /G Do", extra=objects),
                maker("/G Do", extra=control),
                (1, 2, 1, 1, 1),
                failure=True,
            )

        integral = "BT /F 24 Tf 20 30 Td (A) Tj ET"
        pair(
            "integral glyph origin",
            truetype(CHAIN + integral),
            truetype(DIRECT + integral),
            reference=True,
        )

        # The same large-coordinate cancellation must work before path storage.
        vectors = (
            f"{BIG+20.125:.3f} 30.125 10.5 14.25 re f",
            f"{BIG+20.125:.3f} 30.125 m {BIG+30.625:.3f} 30.125 l {BIG+25.375:.3f} 44.375 l h f",
            f"{BIG+20.125:.3f} 30.125 m {BIG+30.625:.3f} 30.125 {BIG+30.625:.3f} 44.375 {BIG+20.125:.3f} 44.375 c h f",
        )
        controls = (
            "20.125 30.125 10.5 14.25 re f",
            "20.125 30.125 m 30.625 30.125 l 25.375 44.375 l h f",
            "20.125 30.125 m 30.625 30.125 30.625 44.375 20.125 44.375 c h f",
        )
        for index, (large, small) in enumerate(zip(vectors, controls)):
            for aa in (0, 1):
                pair(
                    ("path", index, aa),
                    document("/MediaBox [0 0 120 90]", stream=f"1 0 0 1 {-BIG} 0 cm " + large),
                    document("/MediaBox [0 0 120 90]", stream=small),
                    (aa, 0, 0, 0, aa),
                    failure=True,
                    reference=index == 0 and aa == 0,
                )
        for axis in (0, 1):
            x, y = (BIG, 0) if axis == 0 else (0, BIG)
            content = f"{x+20.125:.3f} {y+30.125:.3f} 10.5 14.25 re f"
            pair(
                ("page origin", axis),
                document(f"/MediaBox [{x} {y} {x+120} {y+90}]", stream=content),
                document("/MediaBox [0 0 120 90]", stream=controls[0]),
                (1, 0, 0, 0, 1),
                failure=True,
            )

        pair(
            "wide stroke edge",
            document(
                "/MediaBox [0 0 120 90]",
                stream="32767.998046875 w 16384.0009765625 20.125 m 16384.0009765625 40.125 l S",
            ),
            document(
                "/MediaBox [0 0 120 90]",
                stream="400 w 200.001953125 20.125 m 200.001953125 40.125 l S",
            ),
            (1, 0, 0, 0, 1),
            failure=True,
        )
        # Higher precision does not remove the existing coverage-coordinate cap.
        path.write_bytes(
            document(
                "/MediaBox [0 0 120 90]",
                stream="524287.96875 w 262144.015625 20.125 m 262144.015625 40.125 l S",
            )
        )
        rejected = subprocess.run(
            [probe, str(path), "0", "1", "0", "0", "0", "1"], capture_output=True, timeout=30
        )
        assert rejected.returncode == 3 and not rejected.stdout
        rejections += 1
        appearances = []
        for prefix in (CHAIN, DIRECT):
            page = PAGE + b"<< /Font << /F 5 0 R >> >> /Annots [23 0 R] >>"
            objects = {
                3: page,
                23: b"<< /Type /Annot /Subtype /Stamp /Rect [0 0 120 90] /AP << /N 20 0 R >> >>",
                20: stream(
                    prefix + TEXT,
                    "/Type /XObject /Subtype /Form /BBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> >>",
                ),
            }
            appearances.append(truetype("", extra=objects))
        pair("annotation appearance", *appearances, (1, 2, 1, 1, 1), failure=True)

        # Pattern and mask programs run in their own inherited geometry state.
        for context in ("pattern", "mask", "image", "shading"):
            results = []
            for prefix in (CHAIN, DIRECT):
                objects = {3: PAGE + RESOURCES}
                if context == "pattern":
                    objects[21] = stream(
                        prefix + TEXT,
                        "/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 120 90] /XStep 120 /YStep 90 /Matrix [.75 0 0 .75 0 0] /Resources << /Font << /F 5 0 R >> >>",
                    )
                    content = "/Pattern cs /P scn 0 0 120 90 re f"
                elif context == "mask":
                    objects[20] = stream(
                        prefix + "1 g " + TEXT,
                        "/Type /XObject /Subtype /Form /BBox [0 0 120 90] /Group << /S /Transparency /I true /CS /DeviceGray >> /Resources << /Font << /F 5 0 R >> >>",
                    )
                    objects[22] = b"<< /Type /ExtGState /SMask << /S /Alpha /G 20 0 R >> >>"
                    content = "/Mask gs 1 0 0 rg 0 0 120 90 re f"
                elif context == "image":
                    objects[30] = stream(
                        bytes((255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0)),
                        "/Type /XObject /Subtype /Image /Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB",
                    )
                    content = prefix + "20 0 0 20 20.1875 30.1875 cm /Im Do"
                else:
                    objects[3] = (PAGE + RESOURCES).replace(
                        b"/Pattern", b"/Shading << /S 31 0 R >> /Pattern"
                    )
                    objects[31] = (
                        b"<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [20.1875 0 80.1875 0] /Function 32 0 R /Extend [true true] >>"
                    )
                    objects[32] = (
                        b"<< /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >>"
                    )
                    content = prefix + "/S sh"
                results.append(truetype(content, extra=objects))
            pair(context, *results, (1, 2, 1, 1, 1), failure=True)
    print(
        f"PDF geometry precision: {checks} page pairs, {allocations} allocation failures, "
        f"{references} independent equivalent-page pairs, {reference_differences} reference phase differences, "
        f"{rejections} retained-bound rejections"
    )


if __name__ == "__main__":
    main()
