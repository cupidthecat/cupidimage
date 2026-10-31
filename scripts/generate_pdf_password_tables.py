#!/usr/bin/env python3
"""Generate the Unicode 3.2 normalization and RFC 3454 profile data."""

from pathlib import Path
import stringprep
import sys
import unicodedata

ROOT = Path(__file__).resolve().parents[1]
UCD = unicodedata.ucd_3_2_0


def generate():
    assert UCD.unidata_version == "3.2.0"
    decompositions, values, combining, compositions, ranges = [], [], [], [], []
    prohibited = [
        getattr(stringprep, "in_table_" + table)
        for table in ("c21", "c22", "c3", "c4", "c5", "c6", "c7", "c8", "c9")
    ]
    for code in range(0x110000):
        char = chr(code)
        flags = 1 if stringprep.in_table_b1(char) else 0
        flags |= 2 if stringprep.in_table_c12(char) else 0
        flags |= 4 if stringprep.in_table_a1(char) or any(fn(char) for fn in prohibited) else 0
        flags |= 8 if stringprep.in_table_d1(char) else 0
        flags |= 16 if stringprep.in_table_d2(char) else 0
        if flags:
            if ranges and ranges[-1][1] == code - 1 and ranges[-1][2] == flags:
                ranges[-1][1] = code
            else:
                ranges.append([code, code, flags])
        ccc = UCD.combining(char)
        if ccc:
            combining.append((code, ccc))
        normal = UCD.normalize("NFKD", char)
        assert len(normal) <= 18
        if normal != char and not 0xAC00 <= code < 0xD7A4:
            decompositions.append((code, len(values), len(normal)))
            values.extend(map(ord, normal))
        canonical = UCD.decomposition(char)
        if canonical and not canonical.startswith("<"):
            seq = [int(value, 16) for value in canonical.split()]
            if len(seq) == 2 and UCD.normalize("NFC", "".join(map(chr, seq))) == char:
                compositions.append((*seq, code))
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
