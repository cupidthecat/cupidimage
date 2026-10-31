#!/usr/bin/env python3
"""Reject nonrepresentable inverse coordinates before sampling PDF pixels."""

import itertools
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_pages import document
from test_pdf_type3 import stream


def extreme():
    big = "1" + "0" * 50
    small = "0." + "0" * 49 + "1"
    return (
        f"1 0 0 {big} 0 0 cm 1 {big} 0 1 0 0 cm "
        + f"{big} 0 0 {small} 0 0 cm " * 4
        + "1 0 0 1 -.5 -.5 cm "
    )


def cases(prefix=None):
    prefix = extreme() if prefix is None else prefix
    page = "/MediaBox [0 0 120 90] /Resources << /XObject << /Im 8 0 R /G 9 0 R >> /Shading << /S 10 0 R >> /Font << /F 12 0 R >> >>"
    base = {
        8: stream(
            bytes((255, 0, 0) * 4),
            "/Type /XObject /Subtype /Image /Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB",
        ),
        9: stream("0 0 1 1 re f", "/Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources << >>"),
        10: b"<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 1 0] /Function 11 0 R /Extend [true true] >>",
        11: b"<< /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >>",
        12: b"<< /Type /Font /Subtype /Type1 /BaseFont /OwnedUnknownFace >>",
    }
    for paint in ("/Im Do", "/G Do", "/S sh", "BT /F 1 Tf (A) Tj ET"):
        yield paint, document(page, stream=prefix + paint, extra=base), True
    for option in ("/Interpolate true", "/Mask [255 255 0 0 0 0]"):
        objects = dict(base)
        objects[8] = stream(
            bytes((255, 0, 0) * 4),
            "/Type /XObject /Subtype /Image /Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB "
            + option,
        )
        yield option, document(page, stream=prefix + "/Im Do", extra=objects), True
    objects = dict(base)
    objects[8] = stream(
        b"\x00\x00",
        "/Type /XObject /Subtype /Image /Width 2 /Height 2 /BitsPerComponent 1 /ImageMask true",
    )
    yield "stencil", document(page, stream=prefix + "/Im Do", extra=objects), True
    # A genuinely singular, finite transform still paints nothing.
    for paint in ("/Im Do", "/G Do", "/S sh"):
        yield "singular " + paint, document(
            page, stream=prefix + "1 0 0 0 0 0 cm " + paint, extra=base
        ), False
    big = "1" + "0" * 50
    overflow = f"{big} 0 0 {big} 0 0 cm " * 4
    yield "Form determinant overflow", document(page, stream=overflow + "/G Do", extra=base), True


def main():
    probe, allocator = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix="cupid-geometry-limits-") as temporary:
        path = Path(temporary) / "case.pdf"
        for label, data, invalid in cases():
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), "0"], capture_output=True, timeout=30)
            assert result.returncode == (3 if invalid else 0), (
                label,
                result.returncode,
                result.stderr,
            )
            if invalid:
                assert not result.stdout
            else:
                assert len(result.stdout) == 12 + 160 * 120 * 4 and all(
                    v == 255 for v in result.stdout[12:]
                )
            result = subprocess.run(
                [allocator, str(path), str(int(invalid))],
                capture_output=True,
                text=True,
                timeout=120,
            )
            assert result.returncode == 0, (label, result.stdout, result.stderr)
            allocations += int(result.stdout.split("checks")[0].split()[-1])
            checks += 1
    if "--sweep" in sys.argv[3:]:
        outcomes = {0: 0, 3: 0}
        with tempfile.TemporaryDirectory(prefix="cupid-inverse-sweep-") as temporary:
            path = Path(temporary) / "case.pdf"
            for exponent, count, shear, sign in itertools.product(
                (5, 20, 50), range(1, 6), (False, True), (-1, 1)
            ):
                big = "1" + "0" * exponent
                small = "0." + "0" * (exponent - 1) + "1"
                signed = ("-" if sign < 0 else "") + big
                if shear:
                    prefix = (
                        f"1 0 0 {big} 0 0 cm 1 {signed} 0 1 0 0 cm "
                        + f"{big} 0 0 {small} 0 0 cm " * count
                    )
                else:
                    prefix = f"{signed} 0 0 {big} 0 0 cm " * count
                prefix += "1 0 0 1 -.5 -.5 cm "
                for label, data, _ in itertools.islice(cases(prefix), 4):
                    path.write_bytes(data)
                    result = subprocess.run(
                        [probe, str(path), "0"],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.PIPE,
                        timeout=30,
                    )
                    assert result.returncode in outcomes, (
                        exponent,
                        count,
                        shear,
                        sign,
                        label,
                        result.returncode,
                        result.stderr,
                    )
                    outcomes[result.returncode] += 1
        assert all(outcomes.values())
        print(
            f"PDF numerical safety: {sum(outcomes.values())} bounded outcomes {outcomes}; no raster-fidelity claim"
        )
    print(f"PDF inverse geometry limits: {checks} page checks, {allocations} allocation failures")


if __name__ == "__main__":
    main()
