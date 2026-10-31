#!/usr/bin/env python3
"""Replay pinned, independently rendered monochrome masks without optional packages."""

import base64
import json
from pathlib import Path
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]


def main():
    fixture = json.loads((ROOT / "tests/fixtures/pdf-monochrome-reference.json").read_text())
    requests, expectations = [], []
    for case in fixture["cases"]:
        points = case["points"]
        for mode, mask in enumerate(case["masks"]):
            requests.append(
                f"{len(points)} {case['width']} {case['height']} {mode} {case['precision']}\n"
            )
            requests.extend(f"{x} {y} {tag} {end}\n" for x, y, tag, end in points)
            expectations.append(zlib.decompress(base64.b64decode(mask)))
    result = subprocess.run(
        [sys.argv[1], "--batch"], input="".join(requests).encode(), capture_output=True, check=True
    )
    at = 0
    for index, expected in enumerate(expectations):
        actual = result.stdout[at : at + len(expected)]
        assert actual == expected, (index, sum(a != b for a, b in zip(actual, expected)))
        at += len(expected)
    assert at == len(result.stdout)
    print(f"Monochrome raster: {len(expectations)} stored independent bitmap checks")


if __name__ == "__main__":
    main()
