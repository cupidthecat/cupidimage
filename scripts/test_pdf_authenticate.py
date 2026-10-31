#!/usr/bin/env python3
"""Check PDF password algorithms with independently generated synthetic credentials."""

import argparse
import json
from pathlib import Path
import random
import subprocess
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/pdf-authentication-reference.json"


def generate():
    from pypdf._encryption import AlgV4, AlgV5

    rng = random.Random(320007064)
    cases = []
    for revision in range(2, 7):
        for size in (0, 1, 31, 32, 33, 126, 127, 128):
            for metadata in (0, 1):
                user, owner = rng.randbytes(size), rng.randbytes(size + 1)
                key_length = 5 if revision == 2 else 32 if revision >= 5 else rng.choice((5, 8, 16))
                permissions = rng.choice((0xFFFFFFFC, 0xFFFFF0C0, 0xFFFFFFF4))
                identifier = rng.randbytes(16)
                if revision <= 4:
                    okey = AlgV4.compute_O_value_key(owner, revision, key_length * 8)
                    oval = AlgV4.compute_O_value(okey, user, revision)
                    key = AlgV4.compute_key(
                        user,
                        revision,
                        key_length * 8,
                        oval,
                        permissions,
                        identifier,
                        bool(metadata),
                    )
                    uval = AlgV4.compute_U_value(key, revision, identifier)
                    values = {"/O": oval, "/U": uval, "/OE": b"", "/UE": b"", "/Perms": b""}
                else:
                    key = rng.randbytes(key_length)
                    with patch("secrets.token_bytes", side_effect=rng.randbytes):
                        values = AlgV5.generate_values(
                            revision, user, owner, key, permissions, bool(metadata)
                        )
                cases.append(
                    dict(
                        revision=revision,
                        key_length=key_length,
                        permissions=permissions,
                        metadata=metadata,
                        identifier=identifier.hex(),
                        user_password=user.hex(),
                        owner_password=owner.hex(),
                        key=key.hex(),
                        values={k: v.hex() for k, v in values.items()},
                    )
                )
    FIXTURE.write_text(
        json.dumps({"generator": "pypdf 6.21.4 password algorithms", "cases": cases}, indent=2)
        + "\n"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", nargs="?")
    parser.add_argument("--generate", action="store_true")
    args = parser.parse_args()
    if args.generate:
        generate()
        return
    if not args.probe:
        parser.error("probe is required")
    requests, expected = [], []
    for c in json.loads(FIXTURE.read_text())["cases"]:
        for password, result in (
            (c["user_password"], "0 " + c["key"]),
            (c["owner_password"], "1 " + c["key"]),
            ("77726f6e672070617373776f7264", "invalid"),
        ):
            fields = [
                c["revision"],
                c["key_length"],
                c["permissions"],
                c["metadata"],
                password or "-",
                c["identifier"],
            ]
            fields += [c["values"][key] or "-" for key in ("/O", "/U", "/OE", "/UE", "/Perms")]
            requests.append(" ".join(map(str, fields)) + "\n")
            expected.append(result)
        if c["revision"] >= 5:
            fields[4] = c["user_password"] or "-"
            for byte in range(16):
                mutated = bytearray.fromhex(c["values"]["/Perms"])
                mutated[byte] ^= 1
                fields[-1] = mutated.hex()
                requests.append(" ".join(map(str, fields)) + "\n")
                expected.append("invalid")
    run = subprocess.run(
        [str(Path(args.probe).resolve())],
        input="".join(requests),
        text=True,
        capture_output=True,
        check=True,
    )
    lines = run.stdout.splitlines()
    assert len(lines) == len(expected), (len(lines), len(expected), run.stderr)
    for index, (actual, want) in enumerate(zip(lines, expected)):
        assert actual == want, (index, actual, want)
    print(f"PDF password authentication: {len(expected)} independent checks")


if __name__ == "__main__":
    main()
