#!/usr/bin/env python3
"""Check bounded font-policy metadata against owned SFNT name/table controls."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from test_pdf_subset_fonts import FONT, repack
from test_pdf_truetype import tables


def names(records):
    records = sorted(records, key=lambda row: row[:4])
    result = bytearray(struct.pack(">HHH", 0, len(records), 6 + 12 * len(records)))
    strings = bytearray()
    for platform, encoding, language, identifier, value in records:
        data = value.encode("latin1" if platform == 1 else "utf-16-be")
        result += struct.pack(
            ">HHHHHH", platform, encoding, language, identifier, len(data), len(strings)
        )
        strings += data
    return bytes(result + strings)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path)
    parser.add_argument("--reference", action="store_true")
    args = parser.parse_args()
    if args.reference:
        import freetype
    root = Path("third_party/freetype-tt-policy")
    manifest = json.loads((root / "manifest.json").read_text())
    for item in manifest["files"]:
        assert hashlib.sha256((root / item["path"]).read_bytes()).hexdigest() == item["sha256"]
    checks = references = 0
    with tempfile.TemporaryDirectory(prefix="cupid-tt-policy-") as temporary:
        path = Path(temporary) / "font.ttf"

        def check(data, expected):
            nonlocal checks, references
            path.write_bytes(data)
            result = subprocess.run(
                [str(args.probe.resolve()), str(path)], capture_output=True, text=True, timeout=30
            )
            assert result.returncode == 0 and int(result.stdout.split()[0]) == expected, (
                expected,
                result.stdout,
                result.stderr,
            )
            checks += 1
            if args.reference:
                face = freetype.Face.from_bytes(data)
                actual = bool(face.face_flags & freetype.FT_FACE_FLAG_TRICKY)
                assert actual == bool(expected), (face.family_name, expected, actual)
                references += 1

        root = Path(__file__).resolve().parents[1] / "third_party/freetype-tt-policy"
        family_names = re.findall(r'^    "([^"]+)"', (root / "family-names.inc").read_text(), re.M)
        for name in family_names:
            for platform, encoding, language in (
                (3, 1, 0x409),
                (3, 10, 0x409),
                (1, 0, 0),
                (0, 3, 0),
            ):
                check(
                    repack(
                        FONT,
                        {
                            b"name": names(
                                [(platform, encoding, language, 1, "ABCDEF+Owned " + name)]
                            )
                        },
                    ),
                    1,
                )
        for text, expected in (
            ("DLCDEF+Ordinary", 0),
            ("DFKai", 0),
            ("dfkai-sb", 0),
            ("HuaTianKaiTi®", 1),
            ("\0DFKai-SB", 0),
        ):
            check(repack(FONT, {b"name": names([(3, 1, 0x409, 1, text)])}), expected)
        a = (3, 1, 0x409, 1, "DFKai-SB")
        for records, expected in (
            ([a, (3, 1, 0x409, 16, "Ordinary")], 0),
            ([(3, 1, 0x409, 1, "Ordinary"), (3, 1, 0x409, 16, "DFKai-SB")], 1),
            ([a, (3, 1, 0x409, 16, "\0")], 0),
            ([a, (3, 1, 0x409, 21, "Ordinary")], 0),
            ([(1, 0, 0, 1, "Ordinary"), a], 1),
            ([(1, 0, 0, 1, "Ordinary"), (3, 1, 0x411, 1, "DFKai-SB")], 0),
            ([(1, 0, 0, 1, "DFKai-SB"), (3, 1, 0x411, 1, "Ordinary")], 1),
            ([(3, 1, 0x409, 1, "Ordinary"), (3, 1, 0x809, 1, "DFKai-SB")], 1),
        ):
            check(repack(FONT, {b"name": names(records)}), expected)
        _, at, length = tables(FONT)[b"OS/2"]
        os2 = bytearray(FONT[at : at + length])
        struct.pack_into(">H", os2, 62, 256)
        check(
            repack(FONT, {b"OS/2": bytes(os2), b"name": names([a, (3, 1, 0x409, 21, "Ordinary")])}),
            1,
        )
        pairs = re.findall(
            r"\{(0x[0-9A-Fa-f]+)u, (0x[0-9A-Fa-f]+)u\}", (root / "program-ids.inc").read_text()
        )
        assert len(pairs) == 93
        for i in range(0, len(pairs), 3):
            changed = {b"name": None}
            for tag, (checksum, length) in zip((b"cvt ", b"fpgm", b"prep"), pairs[i : i + 3]):
                count = int(length, 16)
                changed[tag] = (
                    int(checksum, 16).to_bytes(4, "big") + bytes(count - 4) if count else None
                )
            good = repack(FONT, changed)
            check(good, 1)
            bad = bytearray(good)
            offset = tables(good)[b"fpgm"][1]
            bad[offset] ^= 1
            # Directory checksum intentionally remains unchanged: use the actual table bytes.
            check(bytes(bad), 0)
    print(f"TT profile metadata: {checks} owned fonts, {references} independent classifications")


if __name__ == "__main__":
    main()
