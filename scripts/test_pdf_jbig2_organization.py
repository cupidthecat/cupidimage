#!/usr/bin/env python3
"""Check embedded ordering, standalone compatibility, stripes and custom-table selectors."""

import argparse
import base64
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from pdf_jbig2_fixture import (
    segment,
    packed,
    bit_bytes,
    standard_huffman,
    integer_decisions,
    identifier_decisions,
    encode_many,
)
from test_pdf_jbig2_text import SYMBOLS


def split_segments(data):
    parts = []
    while data:
        number, flags, count = struct.unpack_from(">IBB", data)
        count >>= 5
        assert count <= 4
        width = 1 if number <= 256 else 2 if number <= 65536 else 4
        header = 6 + count * width + (4 if flags & 64 else 1) + 4
        length = int.from_bytes(data[header - 4 : header], "big")
        parts.append((data[:header], data[header : header + length]))
        data = data[header + length :]
    return parts


def custom_table(number):
    import re

    root = Path(__file__).resolve().parents[1]
    text = (root / "third_party/jbig2-tables/huffman.inc").read_text()
    all_rows = [
        tuple(map(int, row)) for row in re.findall(r"\{(\d+), (\d+), (\d+), (-?\d+)\}", text)
    ]
    offsets = list(map(int, re.search(r"offsets\[16\] = \{([^}]+)", text).group(1).split(",")))
    rows = all_rows[offsets[number - 1] : offsets[number]]
    normal = [r for r in rows if r[1] != 32 and not r[2]]
    lower = next((r[0] for r in rows if r[2] == 1), 0)
    upper = next((r[0] for r in rows if r[1] == 32 and not r[2]), 0)
    oob = next((r[0] for r in rows if r[2] == 2), 0)
    high = normal[-1][3] + (1 << normal[-1][1])
    data = struct.pack(">Bii", 0x7E + bool(oob), normal[0][3], high)
    data += bytes(value for row in normal for value in row[:2]) + bytes([lower, upper])
    if oob:
        data += bytes([oob])
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("encoder", type=Path)
    parser.add_argument("probe", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sample = json.loads((root / "tests/fixtures/pdf-jbig2.json").read_text())["samples"][0]
    data = base64.b64decode(sample["data"])
    expected = bytes(v ^ 255 for v in base64.b64decode(sample["black_samples"]))
    parts = split_segments(data)
    cases = [
        ("embedded", data, 24, 16, expected),
        ("unordered", b"".join(h + b for h, b in reversed(parts)), 24, 16, expected),
    ]
    end_page = split_segments(segment(3, 49, b""))[0]
    end_file = split_segments(segment(4, 51, b"", page=0))[0]
    all_parts = parts + [end_page, end_file]
    magic = b"\x97JB2\r\n\x1a\n"
    cases.append(
        (
            "sequential",
            magic + b"\1" + struct.pack(">I", 1) + b"".join(h + b for h, b in all_parts),
            24,
            16,
            expected,
        )
    )
    cases.append(
        (
            "random-access",
            magic
            + b"\0"
            + struct.pack(">I", 1)
            + b"".join(h for h, _ in all_parts)
            + b"".join(b for _, b in all_parts),
            24,
            16,
            expected,
        )
    )
    cases.append(
        (
            "sequential-unknown-pages",
            magic + b"\3" + b"".join(h + b for h, b in all_parts),
            24,
            16,
            expected,
        )
    )
    page_header, page_data = parts[0]
    region_header, region_data = parts[1]
    for unknown in (False, True):
        page = bytearray(page_data)
        page[17:19] = struct.pack(">H", 0x800F)
        if unknown:
            page[4:8] = b"\xff" * 4
        payload = (
            page_header + page + region_header + region_data + segment(3, 50, struct.pack(">I", 15))
        )
        cases.append((f"stripe-{unknown}", payload, 24, 16, expected))
    region_header = bytearray(region_header)
    region_header[-4:] = b"\xff" * 4
    region_data = bytearray(region_data)
    region_data[4:8] = struct.pack(">I", 100)
    cases.append(
        (
            "unknown-region-length",
            page_header + page_data + region_header + region_data + struct.pack(">I", 16),
            24,
            16,
            expected,
        )
    )
    one, two, four = [standard_huffman(i) for i in (1, 2, 4)]
    collective = [sum((s[y] for s in SYMBOLS), []) for y in range(3)]
    dictionary = (
        struct.pack(">HII", 0xFD, 3, 3)
        + bit_bytes(four(3) + two(3) + two(0) + two(0) + two(None) + one(0))
        + packed(collective)
        + bit_bytes(one(0) + one(3))
    )
    draw = (
        integer_decisions("DT", 1)
        + integer_decisions("DT", 1)
        + integer_decisions("FS", 0)
        + identifier_decisions(0, 2)
        + integer_decisions("DS", None)
    )
    encoded = encode_many(args.encoder.resolve(), [(16384, draw)])[0]
    source = segment(0, 48, struct.pack(">4IBH", 3, 3, 0, 0, 0, 0))
    source += b"".join(segment(i + 1, 53, custom_table(n)) for i, n in enumerate((4, 2, 1, 1)))
    source += segment(5, 0, dictionary, refs=(1, 2, 3, 4))
    source += segment(6, 6, struct.pack(">4IBHI", 3, 3, 0, 0, 0, 16, 1) + encoded, refs=(5,))
    cases.append(
        ("unused-aggregate-table", source, 3, 3, bytes(v ^ 255 for v in packed(SYMBOLS[0])))
    )
    with tempfile.TemporaryDirectory(prefix="cupid-jbig2-organization-") as temporary:
        path = Path(temporary) / "case.jb2"
        for name, data, w, h, want in cases:
            path.write_bytes(data)
            run = subprocess.run(
                [str(args.probe.resolve()), str(path), str(w), str(h)],
                capture_output=True,
                timeout=15,
            )
            assert run.returncode == 0 and run.stdout == want, (name, run.stderr)
    print(
        f"JBIG2 organization: {len(cases)} exact ordering, stripe, unknown-length and custom-table cases"
    )


if __name__ == "__main__":
    main()
