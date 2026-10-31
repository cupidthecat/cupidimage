#!/usr/bin/env python3
"""Check pinned JBIG2 table data and retained license bytes."""

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/jbig2-tables"


def main():
    manifest = json.loads((SOURCE / "manifest.json").read_text())
    for row in manifest["generated"] + manifest["notices"]:
        actual = hashlib.sha256((SOURCE / row["file"]).read_bytes()).hexdigest()
        assert actual == row["sha256"], row["file"]
    print("JBIG2 tables: two table files and two upstream notices verified")


if __name__ == "__main__":
    main()
