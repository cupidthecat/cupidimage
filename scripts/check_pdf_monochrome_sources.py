#!/usr/bin/env python3
"""Verify reversible, pinned monochrome raster adaptations."""

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/freetype-monochrome"


def main():
    manifest = json.loads((SOURCE / "manifest.json").read_text())
    for row in manifest["files"]:
        data = (SOURCE / row["file"]).read_bytes()
        assert hashlib.sha256(data).hexdigest() == row["adapted_sha256"], row["file"]
        if row["file"] == "ftraster.inc":
            text = data.decode()
            for patch in reversed(manifest["patches"]):
                assert text.count(patch["new"]) == patch["count"], patch["new"][:80]
                text = text.replace(patch["new"], patch["old"])
            data = text.encode()
        assert hashlib.sha256(data).hexdigest() == row["original_sha256"], row["file"]
    print(
        f"Monochrome sources: {len(manifest['files'])} hashes and {len(manifest['patches'])} reversible patches verified"
    )


if __name__ == "__main__":
    main()
