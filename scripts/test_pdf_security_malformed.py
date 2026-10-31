#!/usr/bin/env python3
"""Exercise security dictionary, revision, ciphertext and null/default boundaries."""

import argparse
import base64
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def append_revision(data, objects=(), encrypt=b"8 0 R", identifier=True):
    previous = int(re.findall(rb"startxref\s+(\d+)", data)[-1])
    result = bytearray(data)
    offsets = []
    maximum = 8
    for number, generation, body in objects:
        offsets.append((number, generation, len(result)))
        maximum = max(maximum, number)
        result += f"{number} {generation} obj\n".encode() + body + b"\nendobj\n"
    xref = len(result)
    result += b"xref\n"
    if not offsets:
        result += b"0 1\n0000000000 65535 f \n"
    for number, generation, offset in offsets:
        result += f"{number} 1\n{offset:010} {generation:05} n \n".encode()
    result += (
        f"trailer\n<< /Size {maximum+1} /Root 1 0 R /Prev {previous} /Encrypt ".encode() + encrypt
    )
    if identifier:
        value = b"owned-structural-id".hex().encode()
        result += b" /ID [<" + value + b"><" + value + b">]"
    return bytes(result + b" >>\nstartxref\n" + str(xref).encode() + b"\n%%EOF\n")


def cases():
    fixture = json.loads(
        (ROOT / "tests/fixtures/pdf-security-structure-reference.json").read_text()
    )
    source = next(c for c in fixture["cases"] if c["name"] == "AES_128-generation")
    data = base64.b64decode(source["document"])
    dictionary = re.search(rb"8 0 obj\n(.*?)\nendobj", data, re.S)[1]
    rows = []

    def add(name, body, reject=True, extras=()):
        rows.append((name, append_revision(data, [(8, 0, body), *extras]), reject))

    add("unchanged-encryption-in-new-revision", dictionary, False)
    rows.append(("direct-encryption-dictionary", append_revision(data, encrypt=dictionary), False))
    rows.append(
        ("encryption-reference-chain", append_revision(data, [(9, 0, b"8 0 R")], b"9 0 R"), False)
    )
    add("indirect-revision", dictionary.replace(b"/R 4", b"/R 9 0 R"), False, ((9, 0, b"4"),))
    add(
        "indirect-revision-extra-body",
        dictionary.replace(b"/R 4", b"/R 9 0 R"),
        True,
        ((9, 0, b"4 5"),),
    )
    add(
        "indirect-handler",
        dictionary.replace(b"/Filter /Standard", b"/Filter 9 0 R"),
        False,
        ((9, 0, b"/Standard"),),
    )
    add(
        "indirect-handler-extra-body",
        dictionary.replace(b"/Filter /Standard", b"/Filter 9 0 R"),
        True,
        ((9, 0, b"/Standard /Other"),),
    )
    changes = {
        "unknown-handler": (b"/Filter /Standard", b"/Filter /Other"),
        "unsupported-version": (b"/V 4", b"/V 3"),
        "unsupported-revision": (b"/R 4", b"/R 7"),
        "inconsistent-version-revision": (b"/R 4", b"/R 3"),
        "noninteger-revision": (b"/R 4", b"/R 4.0"),
        "invalid-key-bits": (b"/Length 128", b"/Length 127"),
        "wrong-filter-key-bytes": (b"/Length 16", b"/Length 17"),
        "noninteger-filter-key-bytes": (b"/Length 16", b"/Length 16.0"),
        "unsupported-cipher": (b"/CFM /AESV2", b"/CFM /None"),
        "wrong-cipher-revision": (b"/CFM /AESV2", b"/CFM /AESV3"),
        "invalid-auth-event": (b"/AuthEvent /DocOpen", b"/AuthEvent /Other"),
        "unknown-stream-filter": (b"/StmF /StdCF", b"/StmF /Missing"),
        "unknown-string-filter": (b"/StrF /StdCF", b"/StrF /Missing"),
    }
    for name, (old, new) in changes.items():
        assert old in dictionary
        add(name, dictionary.replace(old, new, 1))
    for key, value in (
        (b"/Filter", b"/Standard"),
        (b"/F#69lter", b"/Standard"),
        (b"/R", b"4"),
        (b"/Length", b"128"),
    ):
        add("duplicate-" + key.decode()[1:], dictionary[:-2] + b" " + key + b" " + value + b" >>")
    add("invalid-metadata-boolean", dictionary[:-2] + b" /EncryptMetadata 1 >>")
    add("null-metadata-default", dictionary[:-2] + b" /EncryptMetadata null >>", False)
    add(
        "indirect-metadata",
        dictionary[:-2] + b" /EncryptMetadata 9 0 R >>",
        False,
        ((9, 0, b"true"),),
    )
    add(
        "indirect-metadata-extra",
        dictionary[:-2] + b" /EncryptMetadata 9 0 R >>",
        True,
        ((9, 0, b"true false"),),
    )
    for key in (b"O", b"U"):
        add(
            "short-" + key.decode(),
            re.sub(rb"/" + key + rb"\s*<[^>]+>", b"/" + key + b" <00>", dictionary),
        )
        add(
            "indirect-" + key.decode(),
            re.sub(rb"/" + key + rb"\s*<[^>]+>", b"/" + key + b" 9 0 R", dictionary),
            True,
            ((9, 0, b"<00>"),),
        )
    rows.append(
        (
            "missing-file-identifier",
            append_revision(data, [(8, 0, dictionary)], identifier=False),
            True,
        )
    )
    content = re.search(rb"4 23 obj\n(.*?)\nendobj", data, re.S)[1]
    payload = re.search(rb"stream\n(.*?)\nendstream", content, re.S)[1]
    for length in (0, 1, 15, 16, 17, 31, len(payload) - 1):
        body = (
            b"<< /Length "
            + str(length).encode()
            + b" >>\nstream\n"
            + payload[:length]
            + b"\nendstream"
        )
        rows.append((f"invalid-aes-length-{length}", append_revision(data, [(4, 23, body)]), True))
    damaged = bytearray(payload)
    damaged[-17] ^= 1
    rows.append(
        (
            "invalid-aes-padding",
            append_revision(data, [(4, 23, content.replace(payload, bytes(damaged)))]),
            True,
        )
    )
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe")
    parser.add_argument("oom", nargs="?")
    args = parser.parse_args()
    expected = struct.pack("=III", 24, 16, 1) + bytes([255, 0, 0, 255]) * 384
    checked = allocations = 0
    with tempfile.TemporaryDirectory(prefix="cupidimage-security-invalid-") as directory:
        for name, data, reject in cases():
            path = Path(directory) / (name + ".pdf")
            path.write_bytes(data)
            result = subprocess.run(
                [str(Path(args.probe).resolve()), str(path)],
                input=b"synthetic-user",
                capture_output=True,
            )
            assert (result.returncode != 0) == reject, (name, result.returncode, result.stderr)
            assert result.stdout == (b"" if reject else expected), name
            checked += 1
            if args.oom and name in (
                "direct-encryption-dictionary",
                "indirect-revision",
                "short-U",
                "invalid-aes-padding",
                "duplicate-Filter",
            ):
                result = subprocess.run(
                    [str(Path(args.oom).resolve()), "--password", str(path), str(int(reject))],
                    input=b"synthetic-user",
                    capture_output=True,
                    check=True,
                )
                text = result.stdout.decode()
                assert "0 failures" in text, text
                allocations += int(text.split(":")[1].split()[0])
    print(f"PDF security boundaries: {checked} cases; {allocations} allocation checks")


if __name__ == "__main__":
    main()
