#!/usr/bin/env python3
"""Check recursive symbol aggregation with shared arithmetic integer contexts."""

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
from pdf_jbig2_fixture import (
    integer_decisions as integer,
    identifier_decisions as identifier,
    dictionary_decisions,
    dictionary_segment,
    encode_many,
    packed,
    bit_bytes,
    standard_huffman,
    segment,
    check_embedded,
)
from test_pdf_jbig2_text import SYMBOLS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("encoder", type=Path)
    parser.add_argument("probe", type=Path)
    parser.add_argument("--file-decoder", type=Path)
    parser.add_argument("--reference-adapter", type=Path)
    args = parser.parse_args()
    requests = [dictionary_decisions(SYMBOLS)]
    commands = [("DH", 3)]
    for index in range(2):
        commands += [
            ("DW", 9 if not index else 6),
            ("AI", 2),
            ("DT", 1),
            ("DT", 1),
            ("FS", 0),
            ("ID", 0 if not index else 3),
            ("RI", 0),
            ("DS", 3),
            ("ID", 1 if not index else 2),
            ("RI", 0),
            ("DS", None),
        ]
    commands += [("DW", None), ("EX", 3), ("EX", 2)]
    events = []
    for name, value in commands:
        events += identifier(value, 3) if name == "ID" else integer(name, value)
    requests.append((16384, events))
    draw = integer("DT", 1) + integer("DT", 1) + integer("FS", 0) + identifier(0, 1)
    draw += integer("DS", 4) + identifier(1, 1) + integer("DS", None)
    requests.append((16384, draw))
    base, arithmetic, draw = encode_many(args.encoder.resolve(), requests)
    tables = {
        name: standard_huffman(n)
        for name, n in (
            ("DH", 4),
            ("DW", 2),
            ("AI", 1),
            ("DT", 11),
            ("FS", 6),
            ("DS", 8),
            ("EX", 1),
        )
    }
    bits = ""
    for name, value in commands:
        bits += (
            f"{value:03b}" if name == "ID" else str(value) if name == "RI" else tables[name](value)
        )
    huffman = bit_bytes(bits)
    first = [row + [0, 0] + SYMBOLS[1][y] + [0] for y, row in enumerate(SYMBOLS[0])]
    second = [row + [0, 0] + SYMBOLS[2][y] + [0] for y, row in enumerate(first)]
    symbols = [first, second]
    expected = struct.pack("=I", 2)
    for symbol in symbols:
        expected += struct.pack("=3I", len(symbol[0]), 3, (len(symbol[0]) + 7) // 8) + packed(
            symbol
        )
    matches = 0
    with tempfile.TemporaryDirectory(prefix="cupid-jbig2-aggregate-") as temporary:
        path = Path(temporary) / "case.jb2"
        for h, encoded in enumerate((arithmetic, huffman)):
            for template in range(2):
                text = f"{h} 1 0 {template} 3 2 2 {len(encoded)}\n"
                text += "".join(
                    "3 3\n" + "".join(" ".join(map(str, row)) + "\n" for row in symbol)
                    for symbol in SYMBOLS
                )
                run = subprocess.run(
                    [str(args.probe.resolve())],
                    input=(text + encoded.hex(" ") + "\n").encode(),
                    capture_output=True,
                    timeout=15,
                )
                assert run.returncode == 0 and run.stdout == expected, (h, template, run.stderr)
                if args.file_decoder or args.reference_adapter:
                    header = struct.pack(">H", h + 2 + (template << 12))
                    if not h:
                        header += b"\x03\xff\xfd\xff\x02\xfe\xfe\xfe"
                    if not template:
                        header += b"\xff\xff\xff\xff"
                    header += struct.pack(">II", 2, 2)
                    page = segment(1, 48, struct.pack(">4IBH", 30, 3, 0, 0, 0, 0))
                    source = (
                        page
                        + dictionary_segment(SYMBOLS, base)
                        + segment(3, 0, header + encoded, refs=(2,))
                    )
                    source += segment(
                        4, 6, struct.pack(">4IBHI", 30, 3, 0, 0, 0, 16, 2) + draw, refs=(3,)
                    )
                    path.write_bytes(source)
                    pixels = [first[y] + [0] * 3 + second[y] + [0] * 3 for y in range(3)]
                    matches += check_embedded(
                        path,
                        packed(pixels),
                        30,
                        3,
                        args.file_decoder,
                        args.reference_adapter,
                        (h, template),
                    )
    print(f"JBIG2 aggregation: 4 exact nested-symbol cases; {matches} independent matches")


if __name__ == "__main__":
    main()
