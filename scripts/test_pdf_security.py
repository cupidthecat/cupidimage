#!/usr/bin/env python3
"""Compare encrypted PDF pages with their unencrypted serialized controls."""

import argparse
import base64
import hashlib
import json
from audit_pdf_corpus import read_ppm
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe")
    parser.add_argument("oom", nargs="?")
    parser.add_argument("--reference", action="store_true")
    parser.add_argument("--cli")
    parser.add_argument("--stream-probe")
    args = parser.parse_args()
    probe = str(Path(args.probe).resolve())
    fixture = json.loads((ROOT / "tests/fixtures/pdf-security-reference.json").read_text())
    checks, allocation_checks = 0, 0
    with tempfile.TemporaryDirectory(prefix="cupidimage-security-") as directory:
        root = Path(directory)
        expected = {}
        for name, encoded in fixture["controls"].items():
            path = root / (name + ".pdf")
            path.write_bytes(base64.b64decode(encoded))
            result = subprocess.run([probe, str(path)], input=b"", capture_output=True, check=True)
            expected[name] = result.stdout
        assert expected["red"] == struct.pack("=III", 24, 16, 1) + bytes([255, 0, 0, 255]) * (
            24 * 16
        )
        for case in fixture["cases"]:
            data = base64.b64decode(case["document"])
            assert hashlib.sha256(data).hexdigest() == case["sha256"]
            path = root / (case["name"] + ".pdf")
            path.write_bytes(data)
            for key in ("user", "owner"):
                result = subprocess.run(
                    [probe, str(path)], input=case[key].encode(), capture_output=True
                )
                assert result.returncode == 0, (case["name"], key, result.stderr)
                assert result.stdout == expected[case["control"]], (case["name"], key)
                checks += 1
            for password in (b"", b"incorrect", b"x" * 4097, b"\xff"):
                result = subprocess.run([probe, str(path)], input=password, capture_output=True)
                assert result.returncode != 0 and not result.stdout, case["name"]
                checks += 1
            if args.oom and case["name"] in (
                "red-RC4-40",
                "red-AES-128",
                "red-AES-256",
                "native-truetype-hinting-AES-128",
            ):
                result = subprocess.run(
                    [str(Path(args.oom).resolve()), "--password", str(path), "0"],
                    input=case["user"].encode(),
                    capture_output=True,
                    check=True,
                )
                text = result.stdout.decode()
                assert "0 failures" in text, text
                allocation_checks += int(text.split(":")[1].split()[0])
        structural = json.loads(
            (ROOT / "tests/fixtures/pdf-security-structure-reference.json").read_text()
        )
        references = divergences = 0
        for case in structural["cases"]:
            data = base64.b64decode(case["document"])
            assert hashlib.sha256(data).hexdigest() == case["sha256"]
            path = root / (case["name"] + ".pdf")
            path.write_bytes(data)
            for key in ("user", "owner"):
                result = subprocess.run(
                    [probe, str(path)], input=case[key].encode(), capture_output=True
                )
                assert result.returncode == 0 and result.stdout == expected["red"], (
                    case["name"],
                    result.stderr,
                )
                checks += 1
            if args.stream_probe and case["mode"] == "metadata":
                result = subprocess.run(
                    [str(Path(args.stream_probe).resolve()), str(path), "6", "0"],
                    input=case["user"].encode(),
                    capture_output=True,
                )
                assert result.returncode == 0, (case["name"], result.stderr)
                assert result.stdout == b'<x:xmpmeta xmlns:x="adobe:ns:meta/"/>'
                checks += 1
            if case["mode"].startswith("embedded-"):
                result = subprocess.run([probe, str(path)], input=b"", capture_output=True)
                if case["mode"] == "embedded-deferred":
                    assert (
                        result.returncode == 0 and result.stdout == expected["red"]
                    ), result.stderr
                else:
                    assert result.returncode != 0 and not result.stdout
                checks += 1
                if args.stream_probe:
                    for password in (case["user"].encode(), b""):
                        result = subprocess.run(
                            [str(Path(args.stream_probe).resolve()), str(path), "6", "0"],
                            input=password,
                            capture_output=True,
                        )
                        assert (result.returncode == 0) == bool(password), result.stderr
                        assert result.stdout == (b"owned attachment" if password else b"")
                        checks += 1
            if args.reference:
                prefix = root / "reference"
                result = subprocess.run(
                    [
                        "pdftoppm",
                        "-r",
                        "96",
                        "-aa",
                        "no",
                        "-aaVector",
                        "no",
                        "-upw",
                        case.get("reference_password", case["user"]),
                        "-singlefile",
                        str(path),
                        str(prefix),
                    ],
                    capture_output=True,
                    check=True,
                )
                w, h, pixels = read_ppm(prefix.with_suffix(".ppm"))
                match = (w, h, pixels) == (24, 16, bytes([255, 0, 0]) * 384)
                if case["mode"] == "crypt-stdcf" and not match:
                    # The named first Crypt filter is required to decrypt before Flate.
                    # Keep the authored pixel oracle when the comparison engine omits it.
                    assert b"Unknown compression method" in result.stderr, result.stderr
                    divergences += 1
                else:
                    assert match, case["name"]
                    references += 1
            if args.oom and case["name"] in (
                "AES_128-object-stream",
                "AES_128-signature",
                "AES_128-crypt-stdcf",
                "AES_128-literal-palette",
            ):
                result = subprocess.run(
                    [str(Path(args.oom).resolve()), "--password", str(path), "0"],
                    input=case["user"].encode(),
                    capture_output=True,
                    check=True,
                )
                text = result.stdout.decode()
                assert "0 failures" in text, text
                allocation_checks += int(text.split(":")[1].split()[0])
        if args.cli:
            cli = str(Path(args.cli).resolve())
            encrypted = root / "AES_128-generation.pdf"
            control = root / "red.pdf"
            reference = subprocess.run(
                [cli, "--sixel", str(control)], capture_output=True, check=True
            ).stdout
            password = root / "synthetic-password"
            password.write_bytes(b"synthetic-user")
            for source in (str(password), "-"):
                result = subprocess.run(
                    [cli, "--sixel", "--pdf-password-file", source, str(encrypted)],
                    input=b"synthetic-user" if source == "-" else b"",
                    capture_output=True,
                )
                assert result.returncode == 0 and result.stdout == reference, result.stderr
                checks += 1
            for secret in (
                b"fixture-secret-that-must-not-appear",
                b"synthetic-user\n",
                b"x" * 4097,
            ):
                result = subprocess.run(
                    [cli, "--sixel", "--pdf-password-file", "-", str(encrypted)],
                    input=secret,
                    capture_output=True,
                )
                assert result.returncode != 0 and not result.stdout
                assert secret not in result.stderr
                checks += 1
            for options in (
                ["--pdf-password-file"],
                ["--pdf-password-raw", str(encrypted)],
                ["--pdf-password-file", str(root / "missing"), str(encrypted)],
                ["--pdf-password-file", "-", "--pdf-password-file", "-", str(encrypted)],
            ):
                result = subprocess.run([cli, *options], input=b"", capture_output=True)
                assert result.returncode != 0 and not result.stdout
                checks += 1
        print(
            f"PDF encrypted pages: {checks} checks; {allocation_checks} allocation checks; {references} reference matches; {divergences} named-filter divergences"
        )


if __name__ == "__main__":
    main()
