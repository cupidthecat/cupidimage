#!/usr/bin/env python3
"""Compare JBIG2 image XObjects and masks against uncompressed PDF controls."""

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib
from pdf_jbig2_fixture import (
    parameters,
    generic_decisions,
    generic_stream,
    packed,
    encode_many,
    dictionary_decisions,
    dictionary_segment,
    segment,
    integer_decisions,
    identifier_decisions,
)
from test_pdf_jbig2_text import SYMBOLS
from test_pdf_images import fixture
from generate_pdf_jbig2_fixture import build as asset_page


def stream(data, attrs=""):
    return f"<< /Length {len(data)} {attrs} >>\nstream\n".encode() + data + b"\nendstream"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("encoder", type=Path)
    parser.add_argument("probe", type=Path)
    parser.add_argument("--oom", type=Path)
    args = parser.parse_args()
    pixels = [[int((x // 3 + y // 2) % 2) for x in range(24)] for y in range(16)]
    p = parameters(24, 16, 0)
    events = integer_decisions("DT", 1) + integer_decisions("DT", 1) + integer_decisions("FS", 0)
    events += identifier_decisions(0, 2) + integer_decisions("DS", None)
    encoded, dict_encoded, text_encoded = encode_many(
        args.encoder.resolve(),
        [generic_decisions(pixels, p), dictionary_decisions(SYMBOLS), (16384, events)],
    )
    generic = generic_stream(p, encoded)
    globals_data = bytearray(dictionary_segment(SYMBOLS, dict_encoded))
    globals_data[6] = 0
    page = segment(1, 48, struct.pack(">4IBH", 3, 3, 0, 0, 0, 0))
    text = struct.pack(">4IBHI", 3, 3, 0, 0, 0, 16, 1) + text_encoded
    symbol_page = page + segment(3, 6, text, refs=(2,))
    count = 0
    with tempfile.TemporaryDirectory(prefix="cupid-jbig2-images-") as directory:
        directory = Path(directory)

        def render(pdf, name, reject=False):
            nonlocal count
            path = directory / (name + ".pdf")
            path.write_bytes(pdf)
            result = subprocess.run(
                [str(args.probe.resolve()), str(path), "0"], capture_output=True, timeout=20
            )
            count += 1
            assert (result.returncode != 0) if reject else (result.returncode == 0), (
                name,
                result.stderr,
            )
            return result.stdout, path

        for mode in range(6):
            attrs = "/Width 24 /Height 16 /BitsPerComponent 1 "
            attrs += "/ImageMask true" if mode >= 4 else "/ColorSpace /DeviceGray"
            attrs += " /Decode [1 0]" if mode % 2 else " /Decode [0 1]"
            content = "1 0 0 rg 18 0 0 12 0 0 cm /Im0 Do"
            control, _ = render(
                fixture(bytes(v ^ 255 for v in packed(pixels)), attrs, content=content),
                f"control-{mode}",
            )
            for chain in range(3):
                data = (
                    generic
                    if chain == 0
                    else zlib.compress(generic) if chain == 1 else generic.hex().encode() + b">"
                )
                filters = (
                    "/JBIG2Decode"
                    if chain == 0
                    else (
                        "[/FlateDecode /JBIG2Decode]"
                        if chain == 1
                        else "[/ASCIIHexDecode /JBIG2Decode]"
                    )
                )
                actual, path = render(
                    fixture(data, attrs + f" /Filter {filters}", content=content),
                    f"generic-{mode}-{chain}",
                )
                assert actual == control, (mode, chain, "image samples differ")
                if args.oom and mode == 0 and chain == 0:
                    subprocess.run(
                        [str(args.oom.resolve()), str(path), "0"],
                        check=True,
                        capture_output=True,
                        timeout=120,
                    )
        raw = bytes(v ^ 255 for v in packed(SYMBOLS[0]))
        attrs = "/Width 3 /Height 3 /BitsPerComponent 1 /ColorSpace /DeviceGray"
        control, _ = render(fixture(raw, attrs), "symbol-control")
        for variant in range(6):
            global_payload = bytes(globals_data) if variant < 2 else zlib.compress(globals_data)
            global_attrs = "" if variant < 2 else "/Filter /FlateDecode"
            extra = {6: stream(global_payload, global_attrs)}
            params = "<< /JBIG2Globals 6 0 R >>"
            filter_name = "/JBIG2Decode"
            payload = symbol_page
            if variant == 1:
                extra[7] = params
                params = "7 0 R"
            if variant == 3:
                extra[7] = params
                extra[8] = "[null 7 0 R]"
                extra[9] = "[/ASCIIHexDecode /JBIG2Decode]"
                params = "8 0 R"
                filter_name = "9 0 R"
                payload = symbol_page.hex().encode() + b">"
            if variant == 4:
                extra[7] = "6 0 R"
                params = "<< /JBIG2Globals 7 0 R >>"
            if variant == 5:
                # Unordered embedded segments are sorted by their segment numbers.
                payload = symbol_page[len(page) :] + page
            actual, path = render(
                fixture(
                    payload, attrs + f" /Filter {filter_name} /DecodeParms {params}", extra=extra
                ),
                f"globals-{variant}",
            )
            assert actual == control, (variant, "global symbol bitmap differs")
            if args.oom and variant == 3:
                subprocess.run(
                    [str(args.oom.resolve()), str(path), "0"],
                    check=True,
                    capture_output=True,
                    timeout=120,
                )
        actual, asset_path = render(asset_page(), "owned-asset")
        control, _ = render(asset_page(raw=True), "owned-asset-control")
        assert actual == control, "owned sample page differs from raw control"
        if args.oom:
            result = subprocess.run(
                [str(args.oom.resolve()), str(asset_path), "0"],
                check=True,
                capture_output=True,
                timeout=180,
            )
            print(result.stdout.decode().strip())
        render(
            fixture(
                generic,
                "/Width 24 /Height 16 /BitsPerComponent 8 /ColorSpace /DeviceGray /Filter /JBIG2Decode",
            ),
            "bad-depth",
            True,
        )
        render(
            fixture(
                generic,
                "/Width 24 /Height 16 /BitsPerComponent 1 /ColorSpace /DeviceRGB /Filter /JBIG2Decode",
            ),
            "bad-components",
            True,
        )
        render(
            fixture(
                generic,
                "/Width 23 /Height 16 /BitsPerComponent 1 /ColorSpace /DeviceGray /Filter /JBIG2Decode",
            ),
            "bad-width",
            True,
        )
        render(fixture(symbol_page, attrs + " /Filter /JBIG2Decode"), "missing-globals", True)
        render(
            fixture(
                symbol_page,
                attrs + " /Filter /JBIG2Decode /DecodeParms << /JBIG2Globals 6 0 R >>",
                extra={
                    6: stream(
                        symbol_page, "/Filter /JBIG2Decode /DecodeParms << /JBIG2Globals 6 0 R >>"
                    )
                },
            ),
            "globals-cycle",
            True,
        )
        inline = (
            "BI /W 24 /H 16 /BPC 1 /CS /G /F [/ASCIIHexDecode /JBIG2Decode] ID "
            + generic.hex()
            + "> EI"
        )
        render(fixture(content=inline), "inline-rejected", True)
    print(
        f"JBIG2 PDF images: {count} image/mask, Decode, filter-chain, globals and rejection checks"
    )


if __name__ == "__main__":
    main()
