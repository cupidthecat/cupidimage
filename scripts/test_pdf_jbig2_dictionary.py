#!/usr/bin/env python3
"""Check direct symbol dictionaries, raw collective bitmaps and MMR height classes."""

import argparse
import base64
import json
from pathlib import Path
import random
import struct
import subprocess
from pdf_jbig2_fixture import (
    integer_decisions,
    generic_decisions,
    parameters,
    encode_many,
    packed,
    standard_huffman,
    bit_bytes,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("encoder", type=Path)
    parser.add_argument("probe", type=Path)
    args = parser.parse_args()
    rng = random.Random(1449265)
    examples, requests = [], []
    for template in range(4):
        for count in (1, 2, 7):
            for height in (1, 7, 16):
                symbols = [
                    [[rng.randrange(2) for x in range(3 + i * 2)] for y in range(height)]
                    for i in range(count)
                ]
                events = integer_decisions("DH", height)
                previous = 0
                for symbol in symbols:
                    events += integer_decisions("DW", len(symbol[0]) - previous)
                    _, bitmap = generic_decisions(
                        symbol, parameters(len(symbol[0]), height, template)
                    )
                    events += [(cx + 16384, bit) for cx, bit in bitmap]
                    previous = len(symbol[0])
                events += integer_decisions("DW", None)
                events += integer_decisions("EX", 0) + integer_decisions("EX", count)
                requests.append((81920, events))
                examples.append((0, template, symbols, None))
    streams = iter(encode_many(args.encoder.resolve(), requests))
    examples = [(h, t, syms, next(streams)) for h, t, syms, _ in examples]
    dh, dw, size = standard_huffman(4), standard_huffman(2), standard_huffman(1)
    for count in (1, 2, 7):
        for height in (1, 7, 16):
            symbols = [
                [[rng.randrange(2) for x in range(3 + i * 2)] for y in range(height)]
                for i in range(count)
            ]
            bits = dh(height)
            previous = 0
            for symbol in symbols:
                bits += dw(len(symbol[0]) - previous)
                previous = len(symbol[0])
            bits += dw(None) + size(0)
            collective = [sum((symbol[y] for symbol in symbols), []) for y in range(height)]
            data = bit_bytes(bits) + packed(collective) + bit_bytes(size(0) + size(count))
            examples.append((1, 0, symbols, data))
    fixtures = json.loads(
        (Path(__file__).resolve().parents[1] / "tests/fixtures/pdf-ccitt.json").read_text()
    )
    for case in fixtures["cases"]:
        if case["k"] != -1 or case["width"] > 100:
            continue
        width, height = case["width"], case["height"]
        raw = base64.b64decode(case["raster"])
        symbol = [
            [(raw[y * ((width + 7) // 8) + x // 8] >> (7 - x % 8)) & 1 for x in range(width)]
            for y in range(height)
        ]
        mmr = base64.b64decode(case["data"])
        data = (
            bit_bytes(dh(height) + dw(width) + dw(None) + size(len(mmr)))
            + mmr
            + bit_bytes(size(0) + size(1))
        )
        examples.append((1, 0, [symbol], data))
    for i, (huffman, template, symbols, data) in enumerate(examples):
        text = f"{huffman} 0 {template} 0 0 {len(symbols)} {len(symbols)} {len(data)}\n"
        result = subprocess.run(
            [str(args.probe.resolve())],
            input=(text + data.hex(" ") + "\n").encode(),
            capture_output=True,
            timeout=10,
        )
        assert result.returncode == 0, (i, result.stderr)
        assert struct.unpack_from("=I", result.stdout)[0] == len(symbols)
        offset = 4
        for symbol in symbols:
            width, height, stride = struct.unpack_from("=3I", result.stdout, offset)
            offset += 12
            assert (width, height, stride) == (
                len(symbol[0]),
                len(symbol),
                (len(symbol[0]) + 7) // 8,
            )
            assert result.stdout[offset : offset + stride * height] == packed(symbol), (
                i,
                "bitmap differs",
            )
            offset += stride * height
        assert offset == len(result.stdout)
    print(f"JBIG2 dictionaries: {len(examples)} exact arithmetic, raw and MMR cases")


if __name__ == "__main__":
    main()
