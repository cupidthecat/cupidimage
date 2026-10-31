#!/usr/bin/env python3
"""Exercise all generic arithmetic templates with owned exact bitmap controls."""

import argparse
from pdf_jbig2_fixture import check_embedded
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile
from pdf_jbig2_fixture import parameters, generic_decisions, encode_many, packed, generic_stream


def cases():
    randomizer = random.Random(144922018)
    output = []
    for template in range(4):
        for typical in (False, True):
            for width, height in ((1, 1), (7, 3), (8, 5), (9, 7), (17, 12), (31, 19)):
                for variant in range(3):
                    p = parameters(width, height, template, False, typical)
                    if variant == 1:
                        p["at"] = [(-1, 0)] * len(p["at"])
                    if variant == 2:
                        p["at"] = [(-128, -128)] * len(p["at"])
                    pixels = [
                        [randomizer.randrange(2) for _ in range(width)] for _ in range(height)
                    ]
                    if typical:
                        pixels[0] = [0] * width
                        for y in range(1, height):
                            if y % 3:
                                pixels[y] = pixels[y - 1][:]
                    output.append((p, pixels, None))
    for typical in (False, True):
        for changed in range(-1, 12):
            p = parameters(19, 13, 0, True, typical)
            if changed >= 0:
                p["at"][changed] = (-1, 0)
            pixels = [[randomizer.randrange(2) for _ in range(19)] for _ in range(13)]
            if typical:
                for y in range(1, 13):
                    if y % 2:
                        pixels[y] = pixels[y - 1][:]
            output.append((p, pixels, None))
    for template in range(4):
        p = parameters(21, 9, template)
        skip = [[int((x + y) % 3 == 0) for x in range(21)] for y in range(9)]
        pixels = [
            [0 if skip[y][x] else randomizer.randrange(2) for x in range(21)] for y in range(9)
        ]
        output.append((p, pixels, skip))
    return output


def native_input(params, encoded, skip):
    fields = [params[k] for k in ("width", "height", "template", "extended", "typical")] + [
        int(skip is not None),
        len(encoded),
    ]
    text = " ".join(map(str, fields)) + "\n"
    text += "".join(f"{x} {y}\n" for x, y in params["at"])
    if skip is not None:
        text += "".join(" ".join(map(str, row)) + "\n" for row in skip)
    return (text + encoded.hex(" ") + "\n").encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("encoder", type=Path)
    parser.add_argument("probe", type=Path)
    parser.add_argument("--reference-adapter", type=Path)
    parser.add_argument("--file-decoder", type=Path)
    args = parser.parse_args()
    encoder = args.encoder.resolve()
    probe = args.probe.resolve()
    examples = cases()
    streams = encode_many(
        encoder, [generic_decisions(pixels, p, skip) for p, pixels, skip in examples]
    )
    comparisons = 0
    with tempfile.TemporaryDirectory(prefix="cupid-jbig2-generic-") as temporary:
        path = Path(temporary) / "case.jb2"
        for index, ((p, pixels, skip), encoded) in enumerate(zip(examples, streams)):
            expected = packed(pixels)
            result = subprocess.run(
                [str(probe)], input=native_input(p, encoded, skip), capture_output=True, timeout=10
            )
            assert result.returncode == 0, (index, p, result.stderr)
            assert result.stdout == expected, (index, p, "native bitmap differs")
            if (args.reference_adapter or args.file_decoder) and skip is None:
                path.write_bytes(generic_stream(p, encoded))
                comparisons += check_embedded(
                    path,
                    expected,
                    p["width"],
                    p["height"],
                    args.file_decoder,
                    args.reference_adapter,
                    (index, p),
                )
    if args.file_decoder:
        print("JBIG2 embedded streams: all eligible cases verified through the document decoder")
    print(
        f"JBIG2 generic regions: {len(examples)} owned bitmap cases; {comparisons} independent matches; skip masks have analytic controls"
    )


if __name__ == "__main__":
    main()
