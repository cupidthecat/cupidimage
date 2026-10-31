#!/usr/bin/env python3
"""Check pattern dictionaries, Gray-coded planes, fixed-point grids and clipping."""

import argparse
from pdf_jbig2_fixture import check_embedded
import base64
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from pdf_jbig2_fixture import parameters, generic_decisions, encode_many, packed, segment


def render(p, patterns, indices):
    output = [[p["default"]] * p["width"] for _ in range(p["height"])]
    for m, row in enumerate(indices):
        for n, index in enumerate(row):
            x = (p["gx"] + m * p["vy"] + n * p["vx"]) // 256
            y = (p["gy"] + m * p["vx"] - n * p["vy"]) // 256
            for oy, line in enumerate(patterns[index]):
                for ox, bit in enumerate(line):
                    if 0 <= x + ox < p["width"] and 0 <= y + oy < p["height"]:
                        old = output[y + oy][x + ox]
                        output[y + oy][x + ox] = [
                            old | bit,
                            old & bit,
                            old ^ bit,
                            1 ^ old ^ bit,
                            bit,
                        ][p["operation"]]
    return packed(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("encoder", type=Path)
    parser.add_argument("probe", type=Path)
    parser.add_argument("--reference-adapter", type=Path)
    parser.add_argument("--reference-min-patterns", type=int, default=1)
    parser.add_argument("--reference-aligned-only", action="store_true")
    parser.add_argument("--file-decoder", type=Path)
    args = parser.parse_args()
    examples, requests = [], []
    for template in range(4):
        for skip in range(2):
            for variant, (pw, gx, gy, vx, vy) in enumerate(
                ((3, 0, 0, 3 * 256, 0), (129, -257, 513, 514, 129), (255, -512, -255, 193, 257))
            ):
                for count in (1, 3, 4):
                    for operation in range(5):
                        ph = 2
                        patterns = [
                            [[int((x + y + i) % 3 != 0) for x in range(pw)] for y in range(ph)]
                            for i in range(count)
                        ]
                        collective = [
                            sum((pattern[y] for pattern in patterns), []) for y in range(ph)
                        ]
                        pattern_p = parameters(pw * count, ph, template)
                        pattern_p["at"][0] = (-pw, 0)
                        requests.append(generic_decisions(collective, pattern_p))
                        p = dict(
                            width=23,
                            height=17,
                            gx=gx,
                            gy=gy,
                            vx=vx,
                            vy=vy,
                            gw=4,
                            gh=3,
                            operation=operation,
                            default=operation % 2,
                            skip=skip,
                            template=template,
                            mmr=0,
                        )
                        indices = [
                            [((m * 4 + n) * 3 + n) % count for n in range(4)] for m in range(3)
                        ]
                        mask = [[0] * 4 for _ in range(3)]
                        if skip:
                            for m in range(3):
                                for n in range(4):
                                    x = (gx + m * vy + n * vx) // 256
                                    y = (gy + m * vx - n * vy) // 256
                                    mask[m][n] = int(
                                        x + pw <= 0 or x >= 23 or y + ph <= 0 or y >= 17
                                    )
                                    if mask[m][n]:
                                        indices[m][n] = 0
                        events = []
                        for bit in reversed(range((count - 1).bit_length())):
                            plane = [
                                [(value ^ (value >> 1)) >> bit & 1 for value in row]
                                for row in indices
                            ]
                            _, ev = generic_decisions(
                                plane, parameters(4, 3, template), mask if skip else None
                            )
                            events += ev
                        requests.append((65536, events))
                        examples.append((p, patterns, indices, None, None, 0, template))
    encoded = encode_many(args.encoder.resolve(), requests)
    examples = [
        (p, pats, indices, encoded[2 * i], encoded[2 * i + 1] if len(pats) > 1 else b"", pm, pt)
        for i, (p, pats, indices, _, __, pm, pt) in enumerate(examples)
    ]
    fixtures = json.loads(
        (Path(__file__).resolve().parents[1] / "tests/fixtures/pdf-ccitt.json").read_text()
    )["cases"]
    cases = [c for c in fixtures if c["k"] == -1 and c["width"] == 24]
    for pattern in range(6):
        case = cases[pattern]
        w, h = case["width"], case["height"]
        raw = base64.b64decode(case["raster"])
        indices = [
            [(raw[y * ((w + 7) // 8) + x // 8] >> (7 - x % 8)) & 1 for x in range(w)]
            for y in range(h)
        ]
        pats = [[[0]], [[1]]]
        # A one-row two-pixel collective pattern: white run 1, black run 1.
        from pdf_jbig2_fixture import bit_bytes

        pattern_data = bit_bytes("001" + "000111" + "010")
        p = dict(
            width=w,
            height=h,
            gx=0,
            gy=0,
            vx=256,
            vy=0,
            gw=w,
            gh=h,
            operation=4,
            default=0,
            skip=0,
            template=0,
            mmr=1,
        )
        examples.append((p, pats, indices, pattern_data, base64.b64decode(case["data"]), 1, 0))
    matches = 0
    with tempfile.TemporaryDirectory(prefix="cupid-jbig2-halftone-") as temporary:
        path = Path(temporary) / "case.jb2"
        for i, (p, patterns, indices, pat_data, data, pat_mmr, pat_template) in enumerate(examples):
            pw, ph, count = len(patterns[0][0]), len(patterns[0]), len(patterns)
            fields = [
                pat_mmr,
                pat_template,
                pw,
                ph,
                count,
                len(pat_data),
                len(data),
                p["width"],
                p["height"],
                p["mmr"],
                p["template"],
                p["skip"],
                p["gx"],
                p["gy"],
                p["vx"],
                p["vy"],
                p["gw"],
                p["gh"],
                p["operation"],
                p["default"],
            ]
            text = " ".join(map(str, fields)) + "\n" + (pat_data + data).hex(" ") + "\n"
            result = subprocess.run(
                [str(args.probe.resolve())], input=text.encode(), capture_output=True, timeout=10
            )
            expected = render(p, patterns, indices)
            assert result.returncode == 0 and result.stdout == expected, (i, p, result.stderr)
            compare = not args.reference_aligned_only or (
                p["gx"] == 0 and p["gy"] == 0 and p["vy"] == 0
            )
            if args.reference_adapter or args.file_decoder:
                page = segment(1, 48, struct.pack(">4IBH", p["width"], p["height"], 0, 0, 0, 0))
                pattern = segment(
                    2,
                    16,
                    struct.pack(">BBBI", pat_mmr + pat_template * 2, pw, ph, count - 1) + pat_data,
                )
                flags = (
                    p["mmr"]
                    + p["template"] * 2
                    + p["skip"] * 8
                    + p["operation"] * 16
                    + p["default"] * 128
                )
                header = struct.pack(
                    ">4IBBIIiiHH",
                    p["width"],
                    p["height"],
                    0,
                    0,
                    0,
                    flags,
                    p["gw"],
                    p["gh"],
                    p["gx"],
                    p["gy"],
                    p["vx"],
                    p["vy"],
                )
                path.write_bytes(page + pattern + segment(3, 22, header + data, refs=(2,)))
                matches += check_embedded(
                    path,
                    expected,
                    p["width"],
                    p["height"],
                    args.file_decoder,
                    (
                        args.reference_adapter
                        if count >= args.reference_min_patterns and compare
                        else None
                    ),
                    (i, p),
                )
    if args.file_decoder:
        print("JBIG2 embedded streams: all eligible cases verified through the document decoder")
    print(f"JBIG2 halftones: {len(examples)} exact bitmap cases; {matches} independent matches")


if __name__ == "__main__":
    main()
