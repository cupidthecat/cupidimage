#!/usr/bin/env python3
"""Verify byte-exact private crypto sources and their symbol renaming."""

import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/bearssl"


def main():
    manifest = json.loads((SOURCE / "manifest.json").read_text())
    names, macros = set(), set()
    for row in manifest["files"]:
        raw = (SOURCE / row["file"]).read_bytes()
        assert hashlib.sha256(raw).hexdigest() == row["sha256"], row["file"]
        if row["file"].endswith(".inc"):
            names.update(re.findall(rb"\bbr_[A-Za-z0-9_]+\b", raw))
        elif row["file"].endswith(".h"):
            macros.update(re.findall(rb"^\s*#\s*define\s+(br_\w+)", raw, re.M))
    namespace = (ROOT / "src/formats/pdf/security/crypto/namespace.h").read_text()
    for raw in names - macros:
        name = raw.decode()
        assert f"#define {name} cupidimage_pdf_crypto_{name[3:]}" in namespace, name
    for raw in macros:
        assert not re.search(r"^#define " + raw.decode() + r"\b", namespace, re.M), raw
    print(f'PDF private crypto: {len(manifest["files"])} source hashes verified')


if __name__ == "__main__":
    main()
