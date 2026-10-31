#!/usr/bin/env python3
"""Check refinement templates, adaptive positions, prediction and reference offsets."""

import argparse
from pdf_jbig2_fixture import check_embedded
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
from pdf_jbig2_fixture import (
    parameters,
    generic_decisions,
    refinement_decisions,
    refinement_stream,
    encode_many,
    packed,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("encoder", type=Path)
    parser.add_argument("probe", type=Path)
    parser.add_argument("--reference-adapter", type=Path)
    parser.add_argument("--file-decoder", type=Path)
    args = parser.parse_args()
    randomizer = random.Random(144922019)
    examples = []
    for template in range(2):
        for typical in range(2):
            for width, height in ((1, 1), (7, 3), (8, 7), (17, 13), (32, 19)):
                for variant in range(4):
                    for dx, dy in ((0, 0), (-3, 2), (5, -1)):
                        reference = [
                            [
                                (
                                    randomizer.randrange(2)
                                    if variant == 0
                                    else int((x // 6 + y // 4) % 2 == 0)
                                )
                                for x in range(width)
                            ]
                            for y in range(height)
                        ]
                        pixels = [
                            [randomizer.randrange(2) for x in range(width)] for y in range(height)
                        ]
                        if variant:
                            for y in range(height):
                                for x in range(width):
                                    if y % 3:
                                        pixels[y][x] = (
                                            reference[y - dy][x - dx]
                                            if (0 <= x - dx < width and 0 <= y - dy < height)
                                            else 0
                                        )
                        at = [(-1, -1), (-1, -1)]
                        if variant == 2:
                            at = [(-1, 0), (1, 1)]
                        if variant == 3:
                            at = [(-128, -128), (127, 127)]
                        p = dict(
                            width=width,
                            height=height,
                            template=template,
                            typical=typical,
                            dx=dx,
                            dy=dy,
                            at=at,
                        )
                        examples.append((p, reference, pixels))
    requests = []
    for p, ref, pixels in examples:
        requests += [
            refinement_decisions(pixels, ref, p),
            generic_decisions(ref, parameters(len(ref[0]), len(ref))),
        ]
    streams = encode_many(args.encoder.resolve(), requests)
    comparisons = 0
    with tempfile.TemporaryDirectory(prefix="cupid-jbig2-refinement-") as temporary:
        path = Path(temporary) / "case.jb2"
        for i, (p, ref, pixels) in enumerate(examples):
            encoded, encoded_ref = streams[2 * i : 2 * i + 2]
            fields = [
                p["width"],
                p["height"],
                len(ref[0]),
                len(ref),
                p["template"],
                p["typical"],
                p["dx"],
                p["dy"],
                len(encoded),
            ]
            text = " ".join(map(str, fields)) + "\n"
            text += "".join(f"{x} {y}\n" for x, y in p["at"])
            text += "".join(" ".join(map(str, row)) + "\n" for row in ref)
            text += encoded.hex(" ") + "\n"
            result = subprocess.run(
                [str(args.probe.resolve())], input=text.encode(), capture_output=True, timeout=10
            )
            expected = packed(pixels)
            assert result.returncode == 0 and result.stdout == expected, (i, p, result.stderr)
            if (args.reference_adapter or args.file_decoder) and p["dx"] == p["dy"] == 0:
                path.write_bytes(refinement_stream(p, ref, encoded_ref, encoded))
                comparisons += check_embedded(
                    path,
                    expected,
                    p["width"],
                    p["height"],
                    args.file_decoder,
                    args.reference_adapter,
                    (i, p),
                )
    if args.file_decoder:
        print("JBIG2 embedded streams: all eligible cases verified through the document decoder")
    print(
        f"JBIG2 refinement: {len(examples)} owned bitmap cases; {comparisons} independent matches"
    )


if __name__ == "__main__":
    main()
