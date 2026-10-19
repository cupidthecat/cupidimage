#!/usr/bin/env python3
"""Check image XObjects using generated PDF files and exact RGBA output."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

from test_pdf_pages import document

RGB = bytes((255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0))
RED, GREEN, BLUE, YELLOW = (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)
GRAY = (128, 128, 128)


def image_stream(payload, attrs, length=None):
    size = str(len(payload) if length is None else length)
    return (f"<< /Type /XObject /Subtype /Image {attrs} /Length {size} >>\nstream\n".encode()
            + payload + b"\nendstream")


def fixture(payload=RGB, attrs="/Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB",
            content="18 0 0 12 0 0 cm /Im0 Do", resource="inline", page_attrs="", length=None,
            extra=None):
    resources = "/Resources << /XObject << /Im0 5 0 R >> >>"
    objects = {5: image_stream(payload, attrs, length)}
    parent = ""
    if resource == "inherited":
        parent, resources = resources, ""
    elif resource == "indirect":
        resources = "/Resources 6 0 R"
        objects.update({6: "<< /XObject 7 0 R >>", 7: "<< /Im0 5 0 R >>"})
    elif resource == "shadowed":
        parent, resources = resources, "/Resources << >>"
    elif resource == "missing":
        resources = ""
    objects.update(extra or {})
    return document(f"/MediaBox [0 0 18 12] {resources} {page_attrs}", parent,
                    "0.5 g -100 -100 300 300 re f " + content, objects)


def predictor_encode(raw, components, predictor):
    row = 2 * components
    result = bytearray()
    for y in range(2):
        kind = predictor - 10 if predictor < 15 else y + 3
        if predictor == 2:
            kind = 1
        else:
            result.append(kind)
        for x in range(row):
            value = raw[y * row + x]
            left = raw[y * row + x - components] if x >= components else 0
            up = raw[(y - 1) * row + x] if y else 0
            ul = raw[(y - 1) * row + x - components] if y and x >= components else 0
            p = left + up - ul
            choices = (left, up, ul)
            paeth = min(enumerate(choices), key=lambda item: (abs(p - item[1]), item[0]))[1]
            correction = (0, left, up, (left + up) // 2, paeth)[kind]
            result.append((value - correction) & 255)
    return bytes(result)


def main():
    probe = str(pathlib.Path(sys.argv[1]).resolve())
    failures = count = 0
    with tempfile.TemporaryDirectory(prefix="cupidimage-pdf-images-") as directory:
        def check(name, pdf, expected=None, dimensions=(24, 16), reject=False):
            nonlocal failures, count
            count += 1
            path = pathlib.Path(directory) / (name + ".pdf")
            path.write_bytes(pdf)
            result = subprocess.run([probe, str(path), "0"], capture_output=True, timeout=15)
            try:
                if reject:
                    assert result.returncode == 3, (result.returncode, result.stderr[:1000])
                    assert result.stderr
                    return
                assert result.returncode == 0, result.stderr[:1000]
                width, height, pages = struct.unpack("=III", result.stdout[:12])
                assert (width, height, pages) == (*dimensions, 1), (width, height, dimensions)
                assert len(result.stdout) == 12 + width * height * 4
                for y in range(height):
                    for x in range(width):
                        offset = 12 + (y * width + x) * 4
                        want = (*expected(x, y), 255)
                        got = tuple(result.stdout[offset:offset + 4])
                        assert got == want, (x, y, got, want)
            except (AssertionError, struct.error) as error:
                failures += 1
                print("FAIL", name, str(error), file=sys.stderr)

        def quadrants(x, y):
            return ((RED, GREEN), (BLUE, YELLOW))[y >= 8][x >= 12]

        attrs = "/Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB"
        for resource in ("inline", "inherited", "indirect"):
            check(resource, fixture(resource=resource), quadrants)
        check("flate", fixture(zlib.compress(RGB), attrs + " /Filter /FlateDecode"), quadrants)
        check("stored-flate", fixture(zlib.compress(RGB, 0), attrs + " /Filter /FlateDecode"), quadrants)
        check("dynamic-to-stored", fixture(bytes.fromhex(
              "780104e0819224499224c9128b9a4756cfde7bffffb988090900f6ff01020304050607080900af002e"),
              "/Width 10 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray /Filter /FlateDecode"),
              lambda x, y: (int((x + .5) / 24 * 10),) * 3)
        for components in (1, 3):
            raw = bytes((0, 85, 170, 255)) if components == 1 else RGB
            space = "DeviceGray" if components == 1 else "DeviceRGB"
            image_attrs = f"/Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /{space}"
            expected = (lambda x, y: ((0, 85), (170, 255))[y >= 8][x >= 12])
            colors = (lambda x, y: (expected(x, y),) * 3) if components == 1 else quadrants
            check(f"raw-{space}", fixture(raw, image_attrs), colors)
            for predictor in (2, 10, 11, 12, 13, 14, 15):
                encoded = zlib.compress(predictor_encode(raw, components, predictor))
                params = f" /Filter /FlateDecode /DecodeParms << /Predictor {predictor} /Columns 2 /Colors {components} /BitsPerComponent 8 >>"
                check(f"predictor-{components}-{predictor}", fixture(encoded, image_attrs + params), colors)
        check("decode-invert", fixture(attrs=attrs + " /Decode [1 0 1 0 1 0]"),
              lambda x, y: tuple(255 - c for c in quadrants(x, y)))
        check("decode-gray", fixture(bytes((0, 85, 170, 255)),
              "/Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceGray /Decode [1 0]"),
              lambda x, y: (255 - ((0, 85), (170, 255))[y >= 8][x >= 12],) * 3)
        check("image-mask-false", fixture(attrs=attrs + " /ImageMask false"), quadrants)
        check("nested-decoys", fixture(attrs="/Other << /Width 999 /Filter /Unsupported >> " + attrs), quadrants)
        check("indirect-dimensions", fixture(attrs=attrs.replace("/Width 2", "/Width 8 0 R"), extra={8: "2"}), quadrants)
        check("indirect-length", fixture(length="8 0 R", extra={8: "12"}), quadrants)
        check("mirror", fixture(content="-18 0 0 12 18 0 cm /Im0 Do"),
              lambda x, y: quadrants(23 - x, y))
        check("rotate-page", fixture(page_attrs="/Rotate 90"),
              lambda x, y: quadrants(y, 15 - x), (16, 24))
        check("crop-page", fixture(page_attrs="/CropBox [9 0 18 12]"),
              lambda x, y: GREEN if y < 8 else YELLOW, (12, 16))
        check("nonzero-origin", fixture(page_attrs="/CropBox [3 3 15 9]"),
              lambda x, y: quadrants(x + 4, y + 4), (16, 8))
        check("nested-transforms", fixture(content="1 0 0 1 9 0 cm 9 0 0 12 0 0 cm /Im0 Do"),
              lambda x, y: GRAY if x < 12 else quadrants((x - 12) * 2, y))
        check("saved-transform", fixture(content="q 9 0 0 12 0 0 cm /Im0 Do Q 9 0 0 12 9 0 cm /Im0 Do"),
              lambda x, y: quadrants((x % 12) * 2, y))
        check("shear", fixture(content="9 0 9 12 0 0 cm /Im0 Do"),
              lambda x, y: quadrants(int((x + .5) * 2 - (15.5 - y) * 1.5), y)
              if 0 <= (x + .5) / 12 - (15.5 - y) / 16 < 1 else GRAY)
        for name, matrix in (("zero-transform", "0 0 0 0 0 0"),
                             ("off-page", "18 0 0 12 1000000000000000000000000000 0"),
                             ("tiny-transform", ".000000000000000001 0 0 .000000000000000001 0 0")):
            check(name, fixture(content=matrix + " cm /Im0 Do"), lambda x, y: GRAY)
        for name, change in (("zero-width", {"attrs": attrs.replace("/Width 2", "/Width 0")}),
                             ("negative-height", {"attrs": attrs.replace("/Height 2", "/Height -1")}),
                             ("fractional-width", {"attrs": attrs.replace("/Width 2", "/Width 2.5")}),
                             ("huge-width", {"attrs": attrs.replace("/Width 2", "/Width 999999999999999999999999")}),
                             ("oversized-image", {"attrs": attrs.replace("/Width 2", "/Width 100000000")}),
                             ("unsupported-bits", {"attrs": attrs.replace("/BitsPerComponent 8", "/BitsPerComponent 1")}),
                             ("unsupported-space", {"attrs": attrs.replace("/DeviceRGB", "/Indexed")}),
                             ("unsupported-filter", {"attrs": attrs + " /Filter /DCTDecode"}),
                             ("unsupported-mask", {"attrs": attrs + " /ImageMask true"}),
                             ("soft-mask", {"attrs": attrs + " /SMask 8 0 R"}),
                             ("color-mask", {"attrs": attrs + " /Mask [0 0 0 0 0 0]"}),
                             ("bad-decode", {"attrs": attrs + " /Decode [1 0]"}),
                             ("short-data", {"payload": RGB[:-1]}),
                             ("extra-data", {"payload": RGB + b"X"}),
                             ("wrong-length", {"length": 999999999}),
                             ("huge-length", {"length": 999999999999999999999999999}),
                             ("bad-flate", {"payload": b"bad", "attrs": attrs + " /Filter /FlateDecode"}),
                             ("wrong-predictor", {"payload": zlib.compress(RGB), "attrs": attrs + " /Filter /FlateDecode /DecodeParms << /Predictor 9 >>"}),
                             ("wrong-columns", {"payload": zlib.compress(RGB), "attrs": attrs + " /Filter /FlateDecode /DecodeParms << /Predictor 2 >>"}),
                             ("bad-row-filter", {"payload": zlib.compress(bytes((5,)) + RGB[:6] + bytes((0,)) + RGB[6:]), "attrs": attrs + " /Filter /FlateDecode /DecodeParms << /Predictor 15 /Columns 2 /Colors 3 >>"}),
                             ("missing-resource", {"resource": "missing"}),
                             ("shadowed-resource", {"resource": "shadowed"}),
                             ("wrong-operand", {"content": "(Im0) Do"})):
            check(name, fixture(**change), reject=True)
    print(f"PDF image regressions: {count} cases, {failures} failures")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main())
