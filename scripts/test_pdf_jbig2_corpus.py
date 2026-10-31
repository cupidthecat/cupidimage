#!/usr/bin/env python3
"""Check hash-pinned JBIG2 PDF pages and independently compared image samples."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("page_probe", type=Path)
    parser.add_argument("sample_probe", type=Path)
    parser.add_argument("--cache", type=Path, default=Path(".cache/jbig2-corpus"))
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root / "tests/fixtures/pdf-jbig2-corpus.json").read_text())
    cache = args.cache / manifest["revision"]
    cache.mkdir(parents=True, exist_ok=True)
    for row in manifest["files"]:
        assert Path(row["name"]).name == row["name"]
        path = cache / row["name"]
        if not path.exists():
            if not args.download:
                raise SystemExit(f"Missing {path}; use --download or supply a verified cache")
            url = urllib.parse.urlsplit(row["url"])
            assert url.scheme == "https" and url.hostname == "raw.githubusercontent.com"
            with urllib.request.urlopen(row["url"], timeout=30) as response:
                data = response.read(16 * 1024 * 1024 + 1)
            assert len(data) <= 16 * 1024 * 1024
            assert hashlib.sha256(data).hexdigest() == row["sha256"], row["name"]
            path.write_bytes(data)
        assert hashlib.sha256(path.read_bytes()).hexdigest() == row["sha256"], row["name"]
        page = subprocess.run(
            [str(args.page_probe.resolve()), str(path), "0"], capture_output=True, timeout=45
        )
        assert page.returncode == 0, (row["name"], page.stderr)
        width, height, count = struct.unpack("=3I", page.stdout[:12])
        assert width and height and count and len(page.stdout) == 12 + width * height * 4
        samples = subprocess.run(
            [
                str(args.sample_probe.resolve()),
                str(path),
                str(row["object"]),
                str(row["generation"]),
                str(row["width"]),
                str(row["height"]),
            ],
            capture_output=True,
            timeout=45,
        )
        assert samples.returncode == 0, (row["name"], samples.stderr)
        assert len(samples.stdout) == ((row["width"] + 7) // 8) * row["height"]
        assert hashlib.sha256(samples.stdout).hexdigest() == row["samples_sha256"], row["name"]
    print(
        f"JBIG2 corpus: {len(manifest['files'])} first pages and exact decoded-sample hashes passed"
    )
    print(
        "Page loading is checked separately; sample equality does not establish full-page fidelity."
    )


if __name__ == "__main__":
    main()
