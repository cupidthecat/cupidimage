#!/usr/bin/env python3
"""Compare the complete bounded password profile with Unicode 3.2 and RFC 4013."""

import argparse
from pathlib import Path
import random
import subprocess
from pdf_unicode32 import load_data, SOURCE

UCD = load_data()


def finish(text):
    flags = [UCD.flags(ord(char)) for char in text]
    if any(value & 6 for value in flags):
        return "invalid"
    if any(value & 8 for value in flags):
        if any(value & 16 for value in flags) or not flags[0] & 8 or not flags[-1] & 8:
            return "invalid"
    return text.encode("utf-8")[:127].hex()


def reference(text):
    text = "".join(
        " " if UCD.contains("C.1.2", ord(c)) else c for c in text if not UCD.contains("B.1", ord(c))
    )
    return finish(UCD.normalize(text))


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
            or UCD.normalize(char) != char
            or UCD.ccc.get(scalar, 0)
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
    # These expectations come directly from the published normalization suite,
    # not from the generator or this module's reference implementation.
    normative = 0
    for line in (SOURCE / "NormalizationTest-3.2.0.txt").read_text(encoding="utf-8").splitlines():
        body = line.split("#", 1)[0].strip()
        if not body or body.startswith("@"):
            continue
        columns = [
            "".join(chr(int(value, 16)) for value in part.split()) for part in body.split(";")[:5]
        ]
        assert len(columns) == 5
        expected_normal = columns[3]
        for text in columns:
            assert UCD.normalize(text) == expected_normal, body
            # SASLprep mapping precedes normalization; the Unicode vectors
            # themselves do not specify that separate mapping step.
            if any(UCD.flags(ord(c)) & 3 for c in text):
                continue
            add(text.encode("utf-8"), finish(expected_normal))
            normative += 1
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
    print(
        f"PDF password preparation: {len(expected)} Unicode/profile checks; {normative} published normalization vectors"
    )


if __name__ == "__main__":
    main()
