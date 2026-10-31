#!/usr/bin/env python3
"""Check private PDF crypto primitives against fixed and independent vectors."""

import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/pdf-crypto-reference.json"


def generate():
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.decrepit.ciphers.algorithms import ARC4

    rng = random.Random(0x32000706)
    cases = []
    for key_length in (16, 24, 32):
        for blocks in (0, 1, 2, 3, 4, 7, 8, 16, 31, 64, 127, 256):
            for _ in range(3):
                key, iv, data = (
                    rng.randbytes(key_length),
                    rng.randbytes(16),
                    rng.randbytes(blocks * 16),
                )
                cipher = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
                result = cipher.update(data) + cipher.finalize()
                cases.append(
                    dict(
                        kind="aes", key=key.hex(), iv=iv.hex(), data=data.hex(), result=result.hex()
                    )
                )
    for key_length in (5, 7, 8, 10, 16):
        for size in (0, 1, 15, 16, 17, 255, 256, 257, 1024, 4096):
            for _ in range(3):
                key, data = rng.randbytes(key_length), rng.randbytes(size)
                cipher = Cipher(ARC4(key), mode=None).encryptor()
                result = cipher.update(data) + cipher.finalize()
                cases.append(
                    dict(kind="rc4", key=key.hex(), iv="", data=data.hex(), result=result.hex())
                )
    FIXTURE.write_text(
        json.dumps({"generator": "cryptography AES-CBC and ARC4", "cases": cases}, indent=2) + "\n"
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

    def add(kind, key, iv, data, result, split=0):
        requests.append(f'{kind} {key or "-"} {iv or "-"} {data or "-"} {split}\n')
        expected.append(result)

    # RFC 6229 section 2, first 128 keystream bits for a 40-bit key.
    add("rc4", "0102030405", "", "00" * 16, "b2396305f03dc027ccc3524a0a1118a8")
    # NIST SP 800-38A F.2.1, AES-128 CBC, four blocks.
    key = "2b7e151628aed2a6abf7158809cf4f3c"
    iv = "000102030405060708090a0b0c0d0e0f"
    plain = (
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52ef"
        "f69f2445df4f9b17ad2b417be66c3710"
    )
    cipher = (
        "7649abac8119b246cee98e9b12e9197d"
        "5086cb9b507219ee95db113a917678b2"
        "73bed6b8e3c1743b7116e69e22229516"
        "3ff1caa1681fac09120eca307586e1a7"
    )
    add("aesenc", key, iv, plain, cipher)
    add("aesdec", key, iv, cipher, plain)
    for case in json.loads(FIXTURE.read_text())["cases"]:
        kind, key, iv, data, result = (case[k] for k in ("kind", "key", "iv", "data", "result"))
        add("aesenc" if kind == "aes" else kind, key, iv, data, result)
        add("aesdec" if kind == "aes" else kind, key, iv, result, data)
    rng = random.Random(0x7995)
    for size in list(range(140)) + [255, 256, 257, 511, 512, 513, 1024, 4096, 32768]:
        data = rng.randbytes(size)
        for algorithm in ("md5", "sha256", "sha384", "sha512"):
            result = hashlib.new(algorithm, data).hexdigest()
            for split in sorted({0, size // 2, size}):
                add(algorithm, "", "", data.hex(), result, split)
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
        assert actual == want, (index, requests[index][:100], actual[:100], want[:100])
    print(f"PDF crypto primitives: {len(expected)} independent/fixed vector checks")


if __name__ == "__main__":
    main()
