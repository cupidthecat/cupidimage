"""Pinned Unicode 3.2 / RFC 3454 data, independent of the host Unicode library."""

from bisect import bisect_right
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/unicode32"


class Unicode32:
    def __init__(self):
        manifest = json.loads((SOURCE / "manifest.json").read_text())
        assert manifest["version"] == "3.2.0"
        for row in manifest["files"]:
            assert (
                hashlib.sha256((SOURCE / row["file"]).read_bytes()).hexdigest() == row["sha256"]
            ), row["file"]
        self.records = {}
        for line in (SOURCE / "UnicodeData-3.2.0.txt").read_text(encoding="utf-8").splitlines():
            row = line.split(";")
            self.records[int(row[0], 16)] = row
        self.ccc = {code: int(row[3]) for code, row in self.records.items() if int(row[3])}
        text = (SOURCE / "CompositionExclusions-3.2.0.txt").read_text(encoding="utf-8")
        exclusions = {int(value, 16) for value in re.findall(r"^([0-9A-F]{4,6})\b", text, re.M)}
        self.compositions = {}
        for code, row in self.records.items():
            value = row[5]
            if not value or value.startswith("<"):
                continue
            sequence = tuple(int(part, 16) for part in value.split())
            if len(sequence) == 2 and not self.ccc.get(sequence[0], 0) and code not in exclusions:
                self.compositions[sequence] = code
        self.tables = json.loads((SOURCE / "stringprep-tables.json").read_text())["tables"]
        self.starts = {
            name: [first for first, last in ranges] for name, ranges in self.tables.items()
        }

    def contains(self, table, scalar):
        ranges = self.tables[table]
        index = bisect_right(self.starts[table], scalar) - 1
        return index >= 0 and scalar <= ranges[index][1]

    @lru_cache(None)
    def decomposition(self, code):
        if 0xAC00 <= code < 0xD7A4:
            offset = code - 0xAC00
            return (0x1100 + offset // 588, 0x1161 + (offset % 588) // 28) + (
                (0x11A7 + offset % 28,) if offset % 28 else ()
            )
        value = self.records.get(code, [""] * 6)[5]
        if not value:
            return (code,)
        parts = [int(part, 16) for part in value.split() if not part.startswith("<")]
        result = []
        for part in parts:
            for scalar in self.decomposition(part):
                result.append(scalar)
                at, combining = len(result) - 1, self.ccc.get(scalar, 0)
                while at and combining and self.ccc.get(result[at - 1], 0) > combining:
                    result[at], result[at - 1] = result[at - 1], result[at]
                    at -= 1
        return tuple(result)

    def compose(self, first, second):
        if 0x1100 <= first < 0x1113 and 0x1161 <= second < 0x1176:
            return 0xAC00 + ((first - 0x1100) * 21 + second - 0x1161) * 28
        if 0xAC00 <= first < 0xD7A4 and (first - 0xAC00) % 28 == 0 and 0x11A7 < second < 0x11C3:
            return first + second - 0x11A7
        return self.compositions.get((first, second))

    def normalize(self, text):
        decomposed = []
        for char in text:
            decomposed.extend(self.decomposition(ord(char)))
        # Stable canonical ordering within each starter-delimited sequence.
        ordered, pending = [], []
        for scalar in decomposed:
            if not self.ccc.get(scalar, 0):
                ordered.extend(sorted(pending, key=lambda c: self.ccc.get(c, 0)))
                pending = []
                ordered.append(scalar)
            else:
                pending.append(scalar)
        ordered.extend(sorted(pending, key=lambda c: self.ccc.get(c, 0)))
        result, starter, last_class = [], None, 0
        for scalar in ordered:
            combining = self.ccc.get(scalar, 0)
            combined = (
                self.compose(result[starter], scalar)
                if starter is not None and (not last_class or last_class < combining)
                else None
            )
            if combined is not None:
                result[starter] = combined
            else:
                if not combining:
                    starter = len(result)
                result.append(scalar)
                last_class = combining
        return "".join(map(chr, result))

    def flags(self, scalar):
        flags = 1 if self.contains("B.1", scalar) else 0
        flags |= 2 if self.contains("C.1.2", scalar) else 0
        prohibited = ("A.1", "C.2.1", "C.2.2", "C.3", "C.4", "C.5", "C.6", "C.7", "C.8", "C.9")
        flags |= 4 if any(self.contains(name, scalar) for name in prohibited) else 0
        flags |= 8 if self.contains("D.1", scalar) else 0
        flags |= 16 if self.contains("D.2", scalar) else 0
        return flags


@lru_cache(None)
def load_data():
    return Unicode32()
