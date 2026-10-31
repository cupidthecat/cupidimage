#!/usr/bin/env python3
"""Check symbol placement across strip sizes, corners, axes and bitmap operators."""

import argparse
from pdf_jbig2_fixture import check_embedded
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from pdf_jbig2_fixture import (
    integer_decisions,
    identifier_decisions,
    dictionary_decisions,
    dictionary_segment,
    encode_many,
    segment,
    packed,
    bit_bytes,
    standard_huffman,
)

SYMBOLS = [
    [[1, 0, 1], [1, 1, 1], [1, 0, 1]],
    [[1, 1, 0], [1, 0, 1], [1, 1, 0]],
    [[0, 1, 1], [1, 0, 0], [0, 1, 1]],
]


def example(huffman, corner, transposed, strips_log, operation, default_pixel=None):
    strips = 1 << strips_log
    default = (corner + operation) % 2 if default_pixel is None else default_pixel
    offset = [-16, 0, 15, -1][corner]
    p = dict(
        width=23,
        height=23,
        instances=6,
        huffman=huffman,
        strips_log=strips_log,
        corner=corner,
        transposed=transposed,
        combination=operation,
        default_pixel=default,
        ds_offset=offset,
    )
    pixels = [[default] * 23 for _ in range(23)]
    commands = [("DT", 1)]
    prior_first = 0
    for line, count in enumerate((3, 2, 1)):
        commands.append(("DT", 1))
        first = -1 if line == 1 else 2
        commands.append(("FS", first - prior_first))
        prior_first = first
        right, bottom = corner >= 2, not (corner & 1)
        before = bottom if transposed else right
        for n in range(count):
            if n:
                commands.append(("DS", 1 - offset))
            within = (n + line) % strips
            if strips_log:
                commands.append(("IT", within))
            ident = (n + line) % 3
            commands.append(("ID", ident))
            s, t = first + n * 3 + (2 if before else 0), line * strips + within
            x, y = (t, s) if transposed else (s, t)
            x -= 2 if right else 0
            y -= 2 if bottom else 0
            for oy, row in enumerate(SYMBOLS[ident]):
                for ox, source in enumerate(row):
                    if 0 <= x + ox < 23 and 0 <= y + oy < 23:
                        old = pixels[y + oy][x + ox]
                        pixels[y + oy][x + ox] = [
                            old | source,
                            old & source,
                            old ^ source,
                            1 ^ old ^ source,
                        ][operation]
        commands.append(("DS", None))
    events, bits = [], ""
    tables = {"DT": standard_huffman(11), "FS": standard_huffman(6), "DS": standard_huffman(8)}
    for name, value in commands:
        if huffman:
            bits += (
                f"{value:02b}"
                if name == "ID"
                else (f"{value:0{strips_log}b}" if name == "IT" else tables[name](value))
            )
        else:
            events += (
                identifier_decisions(value, 2) if name == "ID" else integer_decisions(name, value)
            )
    return p, pixels, (16384, events), bit_bytes(bits)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("encoder", type=Path)
    parser.add_argument("probe", type=Path)
    parser.add_argument("--reference-adapter", type=Path)
    parser.add_argument("--file-decoder", type=Path)
    args = parser.parse_args()
    examples = [
        example(h, c, t, s, op)
        for h in range(2)
        for c in range(4)
        for t in range(2)
        for s in range(4)
        for op in range(4)
    ]
    streams = encode_many(
        args.encoder.resolve(),
        [dictionary_decisions(SYMBOLS)]
        + [entry[2] for entry in examples if not entry[0]["huffman"]],
    )
    dictionary, encoded_iter = streams[0], iter(streams[1:])
    matches = 0
    with tempfile.TemporaryDirectory(prefix="cupid-jbig2-text-") as temporary:
        path = Path(temporary) / "case.jb2"
        for i, (p, pixels, _, huffman_data) in enumerate(examples):
            encoded = huffman_data if p["huffman"] else next(encoded_iter)
            text = (
                " ".join(
                    str(p[k])
                    for k in (
                        "width",
                        "height",
                        "instances",
                        "huffman",
                        "strips_log",
                        "corner",
                        "transposed",
                        "combination",
                        "default_pixel",
                        "ds_offset",
                    )
                )
                + f" 0 0 {len(encoded)}\n"
            )
            text += "".join(" ".join(map(str, row)) + "\n" for symbol in SYMBOLS for row in symbol)
            result = subprocess.run(
                [str(args.probe.resolve())],
                input=(text + encoded.hex(" ") + "\n").encode(),
                capture_output=True,
                timeout=10,
            )
            expected = packed(pixels)
            assert result.returncode == 0 and result.stdout == expected, (i, p, result.stderr)
            if args.reference_adapter or args.file_decoder:
                flags = (
                    p["huffman"]
                    + p["strips_log"] * 4
                    + p["corner"] * 16
                    + p["transposed"] * 64
                    + p["combination"] * 128
                    + p["default_pixel"] * 512
                    + (p["ds_offset"] & 31) * 1024
                )
                data = struct.pack(">4IBH", 23, 23, 0, 0, 0, flags)
                if p["huffman"]:
                    data += b"\0\0"
                data += struct.pack(">I", 6)
                if p["huffman"]:
                    # Three length-two symbol IDs, carried by a one-bit RUNCODE2.
                    data += bit_bytes(
                        "".join("0001" if code == 2 else "0000" for code in range(35)) + "000"
                    )
                data += encoded
                page = segment(1, 48, struct.pack(">4IBH", 23, 23, 0, 0, 0, 0))
                path.write_bytes(
                    page + dictionary_segment(SYMBOLS, dictionary) + segment(3, 6, data, refs=(2,))
                )
                matches += check_embedded(
                    path, expected, 23, 23, args.file_decoder, args.reference_adapter, (i, p)
                )
    if args.file_decoder:
        print("JBIG2 embedded streams: all eligible cases verified through the document decoder")
    print(f"JBIG2 text regions: {len(examples)} exact bitmap cases; {matches} independent matches")


if __name__ == "__main__":
    main()
