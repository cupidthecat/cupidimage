#!/usr/bin/env python3
"""Generate the Unicode 3.2 normalization and RFC 3454 profile data."""

from pathlib import Path
import sys
from pdf_unicode32 import load_data

ROOT = Path(__file__).resolve().parents[1]
UCD = load_data()


def generate():
    decompositions, values, combining, compositions, ranges = [], [], [], [], []
    for code in range(0x110000):
        flags = UCD.flags(code)
        if flags:
            if ranges and ranges[-1][1] == code - 1 and ranges[-1][2] == flags:
                ranges[-1][1] = code
            else:
                ranges.append([code, code, flags])
        ccc = UCD.ccc.get(code, 0)
        if ccc:
            combining.append((code, ccc))
        normal = UCD.decomposition(code)
        assert len(normal) <= 18
        if normal != (code,) and not 0xAC00 <= code < 0xD7A4:
            decompositions.append((code, len(values), len(normal)))
            values.extend(normal)
    compositions = [(first, second, code) for (first, second), code in UCD.compositions.items()]
    rows = [
        "/* Generated Unicode 3.2 data for RFC 3454/4013.",
        " * Unicode License V3: third_party/unicode-codepages/LICENSE. */",
    ]
    for name, width, entries in [
        ("password_flags", 3, ranges),
        ("password_ccc", 2, combining),
        ("password_decompose", 3, decompositions),
        ("password_compose", 3, sorted(compositions)),
    ]:
        rows.append(f"static const uint32_t {name}[][{width}] = {{")
        rows.extend("    {" + ", ".join(f"0x{x:x}" for x in row) + "}," for row in entries)
        rows.append("};")
    rows.append("static const uint32_t password_decomposed[] = {")
    rows.extend(
        "    " + ", ".join(f"0x{x:x}" for x in values[i : i + 8]) + ","
        for i in range(0, len(values), 8)
    )
    rows.append("};")
    return "\n".join(rows) + "\n"


def main():
    path = ROOT / "src/formats/pdf/security/password_data.inc"
    data = generate()
    if "--check" in sys.argv:
        assert path.read_text() == data, "Regenerate PDF password Unicode data"
    else:
        path.write_text(data)
    print("PDF password Unicode 3.2 tables verified")


if __name__ == "__main__":
    main()
