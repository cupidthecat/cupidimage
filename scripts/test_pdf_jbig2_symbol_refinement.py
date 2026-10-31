#!/usr/bin/env python3
"""Exercise refined text instances and single-symbol dictionary refinements."""

import argparse
from pdf_jbig2_fixture import check_embedded
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from pdf_jbig2_fixture import (
    integer_decisions as integer,
    identifier_decisions as identifier,
    refinement_decisions,
    dictionary_decisions,
    dictionary_segment,
    encode_many,
    packed,
    bit_bytes,
    standard_huffman,
    segment,
)
from test_pdf_jbig2_text import SYMBOLS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("encoder", type=Path)
    parser.add_argument("text_probe", type=Path)
    parser.add_argument("dictionary_probe", type=Path)
    parser.add_argument("--reference-adapter", type=Path)
    parser.add_argument("--file-decoder", type=Path)
    args = parser.parse_args()
    one, two, four, six, eight, eleven, fifteen = [
        standard_huffman(i) for i in (1, 2, 4, 6, 8, 11, 15)
    ]
    examples, requests = [], [dictionary_decisions(SYMBOLS)]
    draw = integer("DT", 1) + integer("DT", 1) + integer("FS", 0) + integer("DS", None)
    requests.append((16384, draw))
    for dictionary in range(2):
        for huffman in range(2):
            for template in range(2):
                for variant, (dw, dh, dx, dy) in enumerate(
                    ((-1, 1, -1, 1), (0, -1, 1, -1), (2, 0, 0, 0))
                ):
                    width, height = 3 + dw, 3 + dh
                    pixels = [
                        [int((x + 2 * y + variant) % 3 != 0) for x in range(width)]
                        for y in range(height)
                    ]
                    p = dict(
                        width=width,
                        height=height,
                        template=template,
                        typical=0,
                        dx=dx if dictionary else dw // 2 + dx,
                        dy=dy if dictionary else dh // 2 + dy,
                        at=[(-1, -1), (-1, -1)],
                    )
                    _, refinement = refinement_decisions(pixels, SYMBOLS[0], p)
                    if dictionary:
                        before = integer("DH", height) + integer("DW", width) + integer("AI", 1)
                        before += identifier(0, 2) + integer("RDX", dx) + integer("RDY", dy)
                        after = integer("DW", None) + integer("EX", 3) + integer("EX", 1)
                    else:
                        before = integer("DT", 1) + integer("DT", 1) + integer("FS", 2)
                        before += (
                            identifier(0, 2)
                            + integer("RI", 1)
                            + integer("RDW", dw)
                            + integer("RDH", dh)
                        )
                        before += integer("RDX", dx) + integer("RDY", dy)
                        after = integer("DS", None)
                    events = (
                        refinement
                        if huffman
                        else before + [(cx + 16384, bit) for cx, bit in refinement] + after
                    )
                    requests.append(((1024 if template else 8192) if huffman else 32768, events))
                    examples.append(
                        (dictionary, huffman, template, variant, dw, dh, dx, dy, pixels)
                    )
    streams = encode_many(args.encoder.resolve(), requests)
    base_dictionary, draw = streams[:2]
    matches = 0
    with tempfile.TemporaryDirectory(prefix="cupid-jbig2-symbol-refinement-") as temporary:
        path = Path(temporary) / "case.jb2"
        for index, (example, encoded) in enumerate(zip(examples, streams[2:])):
            dictionary, huffman, template, variant, dw, dh, dx, dy, pixels = example
            width, height = 3 + dw, 3 + dh
            if huffman:
                if dictionary:
                    bits = (
                        four(height)
                        + two(width)
                        + one(1)
                        + "00"
                        + fifteen(dx)
                        + fifteen(dy)
                        + one(len(encoded))
                    )
                    encoded = bit_bytes(bits) + encoded + bit_bytes(two(None) + one(3) + one(1))
                else:
                    bits = eleven(1) + eleven(1) + six(2) + "00" + "1"
                    bits += (
                        fifteen(dw) + fifteen(dh) + fifteen(dx) + fifteen(dy) + one(len(encoded))
                    )
                    encoded = bit_bytes(bits) + encoded + bit_bytes(eight(None))
            if dictionary:
                text = f"{huffman} 1 0 {template} 3 1 1 {len(encoded)}\n"
                text += "".join(
                    "3 3\n" + "".join(" ".join(map(str, row)) + "\n" for row in s) for s in SYMBOLS
                )
                expected = struct.pack("=4I", 1, width, height, (width + 7) // 8) + packed(pixels)
                probe = args.dictionary_probe
            else:
                text = f"23 23 1 {huffman} 0 1 0 0 0 0 1 {template} {len(encoded)}\n"
                text += "".join(" ".join(map(str, row)) + "\n" for s in SYMBOLS for row in s)
                target = [[0] * 23 for _ in range(23)]
                for y, row in enumerate(pixels):
                    target[y][2 : 2 + width] = row
                expected = packed(target)
                probe = args.text_probe
            result = subprocess.run(
                [str(probe.resolve())],
                input=(text + encoded.hex(" ") + "\n").encode(),
                capture_output=True,
                timeout=10,
            )
            assert result.returncode == 0 and result.stdout == expected, (
                index,
                example[:-1],
                result.stderr,
            )
            if args.reference_adapter or args.file_decoder:
                page_width, page_height = (width, height) if dictionary else (23, 23)
                page = segment(1, 48, struct.pack(">4IBH", page_width, page_height, 0, 0, 0, 0))
                source = page + dictionary_segment(SYMBOLS, base_dictionary)
                if dictionary:
                    flags = huffman + 2 + (template << 12)
                    header = struct.pack(">H", flags)
                    if not huffman:
                        header += b"\x03\xff\xfd\xff\x02\xfe\xfe\xfe"
                    if not template:
                        header += b"\xff\xff\xff\xff"
                    header += struct.pack(">II", 1, 1)
                    source += segment(3, 0, header + encoded, refs=(2,))
                    header = struct.pack(">4IBHI", width, height, 0, 0, 0, 16, 1)
                    source += segment(4, 6, header + draw, refs=(3,))
                    want = packed(pixels)
                else:
                    flags = huffman + 2 + 16 + (template << 15)
                    header = struct.pack(">4IBH", 23, 23, 0, 0, 0, flags)
                    if huffman:
                        header += struct.pack(">H", 0x1540)
                    if not template:
                        header += b"\xff\xff\xff\xff"
                    header += struct.pack(">I", 1)
                    if huffman:
                        header += bit_bytes(
                            "".join("0001" if i == 2 else "0000" for i in range(35)) + "000"
                        )
                    source += segment(3, 6, header + encoded, refs=(2,))
                    want = expected
                path.write_bytes(source)
                matches += check_embedded(
                    path,
                    want,
                    page_width,
                    page_height,
                    args.file_decoder,
                    args.reference_adapter,
                    (index, example[:-1]),
                )
    if args.file_decoder:
        print("JBIG2 embedded streams: all eligible cases verified through the document decoder")
    print(f"JBIG2 symbol refinement: {len(examples)} exact cases; {matches} independent matches")


if __name__ == "__main__":
    main()
