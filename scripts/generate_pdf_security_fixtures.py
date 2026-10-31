#!/usr/bin/env python3
"""Generate owned encrypted PDF fixtures with an independent writer."""

import base64
import hashlib
import io
import json
from pathlib import Path
import random
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def generate():
    import pypdf
    from pypdf import PdfReader, PdfWriter
    from pypdf.generic import ArrayObject, ByteStringObject, DecodedStreamObject, NameObject

    assert pypdf.__version__ == "6.21.4", "Use the pinned independent fixture writer"
    rng = random.Random(3200076)
    cases = []
    algorithms = ("RC4-40", "RC4-128", "AES-128", "AES-256-R5", "AES-256")
    sources = (
        "red",
        "native-truetype-hinting",
        "native-cff-hinting",
        "native-type1-hinting",
        "cid-substitute-gb1",
        "descriptor-latin-families",
    )
    controls = {}
    for source in sources:
        for algorithm in (None, *algorithms):
            if source == "red":
                writer = PdfWriter()
                page = writer.add_blank_page(18, 12)
                stream = DecodedStreamObject()
                stream.set_data(b"1 0 0 rg 0 0 18 12 re f")
                page[NameObject("/Contents")] = writer._add_object(stream)
            else:
                writer = PdfWriter(clone_from=PdfReader(ROOT / f"assets/pdf/{source}.pdf"))
            writer._ID = ArrayObject(
                [ByteStringObject(b"owned-security-1"), ByteStringObject(b"owned-security-2")]
            )
            with patch("secrets.token_bytes", side_effect=rng.randbytes):
                if algorithm:
                    writer.encrypt("synthetic-user", "synthetic-owner", algorithm=algorithm)
                out = io.BytesIO()
                writer.write(out)
            data = out.getvalue()
            if algorithm is None:
                controls[source] = base64.b64encode(data).decode()
            else:
                cases.append(
                    dict(
                        name=f"{source}-{algorithm}",
                        control=source,
                        algorithm=algorithm,
                        user="synthetic-user",
                        owner="synthetic-owner",
                        sha256=hashlib.sha256(data).hexdigest(),
                        document=base64.b64encode(data).decode(),
                    )
                )
    path = ROOT / "tests/fixtures/pdf-security-reference.json"
    path.write_text(
        json.dumps(dict(generator="pypdf 6.21.4", controls=controls, cases=cases), indent=2) + "\n"
    )
    print(f"PDF security fixtures: {len(cases)} encrypted documents generated")


if __name__ == "__main__":
    generate()
