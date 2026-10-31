#!/usr/bin/env python3
"""Compare the complete bounded password profile with Unicode 3.2 and RFC 4013."""

import argparse
from pathlib import Path
import random
import stringprep
import subprocess
import unicodedata

UCD = unicodedata.ucd_3_2_0
PROHIBITED = [
    getattr(stringprep, "in_table_" + key)
    for key in ("a1", "c12", "c21", "c22", "c3", "c4", "c5", "c6", "c7", "c8", "c9")
]


def reference(text):
    text = "".join(
        " " if stringprep.in_table_c12(c) else c for c in text if not stringprep.in_table_b1(c)
    )
    text = UCD.normalize("NFKC", text)
    if any(test(c) for c in text for test in PROHIBITED):
        return "invalid"
    if any(stringprep.in_table_d1(c) for c in text):
        if (
            any(stringprep.in_table_d2(c) for c in text)
            or not stringprep.in_table_d1(text[0])
            or not stringprep.in_table_d1(text[-1])
        ):
            return "invalid"
    return text.encode("utf-8")[:127].hex()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe")
    parser.add_argument("--exhaustive", action="store_true")
    args = parser.parse_args()
    requests, expected = [], []

    def add(data, want, revision=6, raw=0):
        requests.append(f'{revision} {raw} {data.hex() or "-"}\n')
        expected.append(want)

    scalars = []
    for scalar in range(0x110000):
        if 0xD800 <= scalar <= 0xDFFF:
            continue
        char = chr(scalar)
        if (
            args.exhaustive
            or scalar < 256
            or UCD.normalize("NFKC", char) != char
            or UCD.combining(char)
            or scalar % 251 == 0
        ):
            scalars.append(char)
            add(char.encode("utf-8"), reference(char))
    samples = [
        "",
        "I\u00adX",
        "user",
        "USER",
        "\u00aa",
        "\u2168",
        "\u0627\u0031",
        "\u0627\u0031\u0628",
        "\u0627a\u0628",
        "\u1100\u1161\u11a8",
        "A\u030a",
        "\u0301\u0327",
        "A\u030a\u0301",
        "A\u0301\u030a",
        "x" * 126 + "\u00e9",
        "\u00a0",
        "\u0007",
        "\u0221",
        "\U0001f600",
    ]
    rng = random.Random(4013)
    for _ in range(1200):
        samples.append("".join(rng.choice(scalars) for _ in range(rng.randrange(1, 20))))
    for text in samples:
        add(text.encode("utf-8"), reference(text))
    for data in (
        b"\x80",
        b"\xc0\x80",
        b"\xc2",
        b"\xed\xa0\x80",
        b"\xf4\x90\x80\x80",
        b"\xf0\x80\x80\x80",
        b"a" * 128 + b"\xff",
        b"a" * 4097,
    ):
        for revision in (2, 4, 5, 6):
            add(data, "invalid", revision)
    for revision in (2, 3, 4):
        for text, want in [
            ("abc", b"abc"),
            ("\u20ac", b"\xa0"),
            ("\u02d8", b"\x18"),
            ("\u2022\ufb01", b"\x80\x93"),
            ("\u00e9", b"\xe9"),
        ]:
            add(text.encode(), want.hex(), revision)
        for text in ("\u00a0", "\u00ad", "\x00", "\u0221"):
            add(text.encode(), "invalid", revision)
        add(bytes(range(256)), bytes(range(32)).hex(), revision, 1)
    for text in samples:
        add(text.encode(), reference(text), 5)
    run = subprocess.run(
        [str(Path(args.probe).resolve())],
        input="".join(requests),
        text=True,
        capture_output=True,
        check=True,
    )
    actual = run.stdout.splitlines()
    assert len(actual) == len(expected), (len(actual), len(expected), run.stderr)
    for index, (value, want) in enumerate(zip(actual, expected)):
        assert value == want, (index, requests[index], value, want)
    print(f"PDF password preparation: {len(expected)} Unicode/profile checks")


if __name__ == "__main__":
    main()
