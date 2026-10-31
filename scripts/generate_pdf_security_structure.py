#!/usr/bin/env python3
"""Generate owned encrypted object-stream and crypt-filter fixtures."""

from pathlib import Path
import pypdf
from pypdf._encryption import Encryption, EncryptAlgorithm
from pypdf.generic import NameObject, BooleanObject
from unittest.mock import patch
import io, random, struct, base64, json, hashlib, zlib, subprocess

root = Path(__file__).resolve().parents[1]
rng = random.Random(320007655)
cases = []
controls = {}
idbytes = b"owned-structural-id"


def build(algorithm, mode, generation=0, user="synthetic-user"):
    enc = (
        Encryption.make(getattr(EncryptAlgorithm, algorithm), 0xFFFFFFFC, idbytes)
        if algorithm
        else None
    )
    encdict = b""
    if enc:
        if mode == "metadata":
            enc.EncryptMetadata = False
        d = enc.write_entry(user, "synthetic-owner")
        if mode == "metadata":
            d[NameObject("/EncryptMetadata")] = BooleanObject(False)
        if mode.startswith("embedded-"):
            d[NameObject("/StmF")] = NameObject("/Identity")
            d[NameObject("/StrF")] = NameObject("/Identity")
            d[NameObject("/EFF")] = NameObject("/StdCF")
            d["/CF"]["/StdCF"][NameObject("/AuthEvent")] = NameObject(
                "/EFOpen" if mode == "embedded-deferred" else "/DocOpen"
            )
        o = io.BytesIO()
        d.write_to_stream(o)
        encdict = o.getvalue()

    def cipher(data, number, gen=0, string=False, identity=False):
        if not enc or identity or (mode.startswith("embedded-") and (number != 6 or string)):
            return data
        filt = enc._make_crypt_filter(number, gen)
        return (filt.str_crypt if string else filt.stm_crypt).encrypt(data)

    def stream(data, number, extra=b"", identity=False, gen=0):
        payload = cipher(data, number, gen, identity=identity)
        return (
            b"<< /Length "
            + str(len(payload)).encode()
            + b" "
            + extra
            + b" >>\nstream\n"
            + payload
            + b"\nendstream"
        )

    def encodedstring(data, number, gen=0, literal=False):
        payload = cipher(data, number, gen, string=True)
        if literal:
            return b"(" + b"".join(("\\%03o" % b).encode() for b in payload) + b")"
        return b"<" + payload.hex().encode() + b">"

    image = mode in (
        "object-stream",
        "literal-palette",
        "hex-palette",
        "signature",
        "signature-untyped",
        "inline",
    )
    content = b"q 18 0 0 12 0 0 cm /Im0 Do Q" if image else b"1 0 0 rg 0 0 18 12 re f"
    if mode == "inline":
        content = (
            b"q 18 0 0 12 0 0 cm BI /W 1 /H 1 /CS /RGB /BPC 8 ID " + bytes([255, 0, 0]) + b" EI Q"
        )
    objects = {
        1: b"<< /Type /Catalog /Pages 2 0 R >>",
        2: b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        3: b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 18 12] /Resources << /XObject << /Im0 5 0 R >> >> /Contents 4 "
        + str(generation).encode()
        + b" R >>",
    }
    extra = b""
    identity = False
    if mode == "crypt-identity":
        extra = b"/Filter /Crypt /DecodeParms << /Name /Identity >>"
        identity = True
    if mode == "crypt-default":
        extra = b"/Filter /Crypt"
        identity = True
    if mode == "crypt-stdcf":
        extra = b"/Filter [/Crypt /FlateDecode] /DecodeParms [<< /Type /CryptFilterDecodeParms /Name /StdCF >> null]"
        content = zlib.compress(content)
    if mode == "metadata":
        objects[1] = b"<< /Type /Catalog /Pages 2 0 R /Metadata 6 0 R >>"
        objects[6] = stream(
            b'<x:xmpmeta xmlns:x="adobe:ns:meta/"/>', 6, b"/Type /Metadata /Subtype /XML", True
        )
    if mode.startswith("embedded-"):
        objects[1] = (
            b"<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles << /Names [(owned.txt) << /Type /Filespec /F (owned.txt) /EF << /F 6 0 R >> >>] >> >> >>"
        )
        objects[6] = stream(b"owned attachment", 6, b"/Type /EmbeddedFile")
    objects[4] = stream(content, 4, extra, identity, generation)
    palette = encodedstring(bytes([255, 0, 0]), 5, literal=mode == "literal-palette")
    if mode == "object-stream":
        palette = b"9 0 R"
    extra = (
        b"/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace [/Indexed /DeviceRGB 0 "
        + palette
        + b"]"
    )
    if mode.startswith("signature"):
        extra += (
            b"/PrivateSignature << "
            + (b"/Type /Sig " if mode == "signature" else b"/ByteRange [0 1 2 3] ")
            + b"/Contents <00> /Reason "
            + encodedstring(b"owned fixture", 5)
            + b" >>"
        )
    objects[5] = stream(b"\x00", 5, extra)
    if enc:
        objects[8] = encdict
    if mode == "object-stream":
        bodies = {n: objects.pop(n) for n in (1, 2, 3)}
        bodies[9] = b"<ff0000>"
        head = b""
        payload = b""
        for number, body in bodies.items():
            head += f"{number} {len(payload)} ".encode()
            payload += body + b"\n"
        objects[6] = stream(
            zlib.compress(head + payload),
            6,
            b"/Type /ObjStm /N 4 /First " + str(len(head)).encode() + b"/Filter/FlateDecode",
        )
    data = bytearray(b"%PDF-1.7\n%\x80\x81\x82\x83\n")
    offsets = {}
    for n, body in sorted(objects.items()):
        offsets[n] = len(data)
        gen = generation if n == 4 else 0
        data += f"{n} {gen} obj\n".encode() + body + b"\nendobj\n"
    trailer = (
        b"/Root 1 0 R /ID [<"
        + idbytes.hex().encode()
        + b"><"
        + idbytes.hex().encode()
        + b">]"
        + (b"/Encrypt 8 0 R" if enc else b"")
    )
    start = len(data)
    if mode == "object-stream":
        offsets[7] = start
        rows = b""
        for n in range(10):
            if n in (1, 2, 3, 9):
                row = (2, 6, (1, 2, 3, 9).index(n))
            elif n in offsets:
                row = (1, offsets[n], generation if n == 4 else 0)
            else:
                row = (0, 0, 65535 if n == 0 else 0)
            rows += struct.pack(">BIH", *row)
        data += (
            b"7 0 obj\n<< /Type /XRef /Size 10 /W [1 4 2] "
            + trailer
            + b"/Length "
            + str(len(rows)).encode()
            + b" >>\nstream\n"
            + rows
            + b"\nendstream\nendobj\n"
        )
    else:
        data += b"xref\n0 9\n0000000000 65535 f \n"
        for n in range(1, 9):
            data += (
                f"{offsets[n]:010} {generation if n==4 else 0:05} n \n".encode()
                if n in offsets
                else b"0000000000 00000 f \n"
            )
        data += b"trailer\n<< /Size 9 " + trailer + b" >>\n"
    data += b"startxref\n" + str(start).encode() + b"\n%%EOF\n"
    return bytes(data)


def main():
    assert pypdf.__version__ == "6.21.4", "Use the pinned independent fixture writer"
    for algorithm in ("RC4_40", "RC4_128", "AES_128", "AES_256_R5", "AES_256"):
        for mode in (
            "generation",
            "object-stream",
            "literal-palette",
            "hex-palette",
            "signature",
            "signature-untyped",
            "inline",
            "crypt-identity",
            "crypt-default",
            "crypt-stdcf",
            "metadata",
            "embedded-deferred",
            "embedded-docopen",
        ):
            if algorithm.startswith("RC4") and mode in (
                "crypt-identity",
                "crypt-default",
                "crypt-stdcf",
                "metadata",
                "embedded-deferred",
                "embedded-docopen",
            ):
                continue
            name = algorithm + "-" + mode
            with patch("secrets.token_bytes", side_effect=rng.randbytes):
                data = build(algorithm, mode, 23 if mode == "generation" else 0)
            cases.append(
                dict(
                    name=name,
                    algorithm=algorithm,
                    mode=mode,
                    user="synthetic-user",
                    owner="synthetic-owner",
                    sha256=hashlib.sha256(data).hexdigest(),
                    document=base64.b64encode(data).decode(),
                )
            )
    for algorithm in ("AES_256_R5", "AES_256"):
        for label, password, prepared in [
            ("mapped", "I\u00adX", "IX"),
            ("composed", "A\u030a", "\u00c5"),
            ("bidi", "\u0627" + "1" + "\u0628", "\u0627" + "1" + "\u0628"),
        ]:
            with patch("secrets.token_bytes", side_effect=rng.randbytes):
                data = build(algorithm, "generation", 23, user=password)
            cases.append(
                dict(
                    name=algorithm + "-unicode-" + label,
                    algorithm=algorithm,
                    mode="unicode",
                    user=password,
                    reference_password=prepared,
                    owner="synthetic-owner",
                    sha256=hashlib.sha256(data).hexdigest(),
                    document=base64.b64encode(data).decode(),
                )
            )
    assets = []
    for algorithm, filename in [
        ("RC4_40", "security-rc4.pdf"),
        ("AES_128", "security-aes128.pdf"),
        ("AES_256", "security-aes256.pdf"),
    ]:
        with patch("secrets.token_bytes", side_effect=rng.randbytes):
            data = build(algorithm, "generation", 23, user="")
        assets.append(
            dict(
                path="assets/pdf/" + filename,
                sha256=hashlib.sha256(data).hexdigest(),
                document=base64.b64encode(data).decode(),
            )
        )
    (root / "tests/fixtures/pdf-security-structure-reference.json").write_text(
        json.dumps(
            {
                "generator": "pypdf 6.21.4 security algorithms; owned PDF structure",
                "cases": cases,
                "public_assets": assets,
            },
            indent=2,
        )
        + "\n"
    )
    print(f"PDF security structure fixtures: {len(cases)} documents generated")


if __name__ == "__main__":
    main()
