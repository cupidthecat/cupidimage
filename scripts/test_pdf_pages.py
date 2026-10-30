#!/usr/bin/env python3
"""Exercise PDF page geometry with complete, generated PDF documents."""

import pathlib
import struct
import subprocess
import sys
import tempfile


def document(page_attrs="", parent_attrs="", stream=None, extra=None, second=False, page_parent=2):
    if stream is None:
        stream = "0.5 g -200 -200 500 500 re f 1 0 0 rg 0 0 9 6 re f"
    content = stream.encode("ascii")
    objects = {
        1: "<< /Type /Catalog /Pages 2 0 R >>",
        2: f"<< /Type /Pages /Count {2 if second else 1} /Kids [3 0 R{' 6 0 R' if second else ''}] {parent_attrs} >>",
        3: f"<< /Type /Page /Parent {page_parent} 0 R /Contents 4 0 R {page_attrs} >>",
        4: b"<< /Length " + str(len(content)).encode() + b" >>\nstream\n" + content + b"\nendstream",
    }
    if second:
        objects[6] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 9 18] /Contents 4 0 R >>"
    objects.update(extra or {})
    result = bytearray(b"%PDF-1.4\n")
    offsets = {}
    for number, body in sorted(objects.items()):
        offsets[number] = len(result)
        result += f"{number} 0 obj\n".encode()
        result += body.encode("ascii") if isinstance(body, str) else body
        result += b"\nendobj\n"
    xref = len(result)
    size = max(objects) + 1
    result += f"xref\n0 {size}\n0000000000 65535 f \n".encode()
    for number in range(1, size):
        offset = offsets.get(number, 0)
        result += f"{offset:010} 00000 {'n' if offset else 'f'} \n".encode()
    result += f"trailer\n<< /Size {size} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
    return result


def main():
    probe = str(pathlib.Path(sys.argv[1]).resolve())
    failures = 0
    count = 0
    with tempfile.TemporaryDirectory(prefix="cupidimage-pdf-pages-") as directory:
        def check(name, data, dimensions=None, samples=(), page=0, pages=1, reject=False):
            nonlocal failures, count
            if reject and "--valid-only" in sys.argv:
                return
            count += 1
            path = pathlib.Path(directory) / (name + ".pdf")
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), str(page)], capture_output=True)
            try:
                if reject:
                    assert result.returncode == 3, (result.returncode, result.stderr[:1000])
                    assert result.stderr
                    return
                assert result.returncode == 0, result.stderr[:1000]
                width, height, actual_pages = struct.unpack("=III", result.stdout[:12])
                assert (width, height) == dimensions, (width, height, dimensions)
                assert actual_pages == pages
                assert len(result.stdout) == 12 + width * height * 4
                for x, y, expected in samples:
                    offset = 12 + (y * width + x) * 4
                    assert tuple(result.stdout[offset:offset + 4]) == (*expected, 255), (x, y, tuple(result.stdout[offset:offset + 4]), expected)
                return width, height, result.stdout[12:]
            except (AssertionError, struct.error) as error:
                failures += 1
                print("FAIL", name, str(error), file=sys.stderr)

        gray = (128, 128, 128)
        red = (255, 0, 0)
        check("fractional-page-extent", document("/MediaBox [0 0 595 842]", stream=""), (794, 1123))
        check("subpixel-page-extent", document("/MediaBox [0 0 .1 .1]", stream=""), (1, 1))
        check("fractional-rotated-page", document("/MediaBox [0 0 595 842] /Rotate 90", stream=""), (1123, 794))
        # Sparse content must not change page geometry or move its origin.
        for angle, dimensions, point in ((0, (600, 800), (306, 396)), (90, (800, 600), (403, 306))):
            check(f"sparse-full-page-{angle}",
                  document(f"/MediaBox [10 20 460 620] /Rotate {angle}", stream="1 0 0 rg 235 320 9 6 re f"),
                  dimensions, ((0, 0, (255, 255, 255)), (*point, red)))
        for angle, dimensions, red_xy, gray_xy in (
                (0, (24, 16), (3, 13), (20, 2)),
                (90, (16, 24), (2, 3), (13, 20)),
                (180, (24, 16), (20, 2), (3, 13)),
                (270, (16, 24), (13, 20), (2, 3)),
                (-90, (16, 24), (13, 20), (2, 3)),
                (450, (16, 24), (2, 3), (13, 20))):
            for inherited in (False, True):
                attrs = f"/MediaBox [0 0 18 12] /Rotate {angle}"
                check(f"rotation-{angle}-inherited-{inherited}",
                      document(parent_attrs=attrs) if inherited else document(page_attrs=attrs),
                      dimensions, ((*red_xy, red), (*gray_xy, gray)))
        check("page-overrides-parent", document("/MediaBox [0 0 18 12] /Rotate 0",
              "/MediaBox [0 0 90 90] /Rotate 90"), (24, 16), ((3, 13, red),))
        check("offset-mediabox", document("/MediaBox [30 60 48 72]",
              stream="0.5 g 30 60 18 12 re f 1 0 0 rg 30 60 9 6 re f"),
              (24, 16), ((3, 13, red), (20, 2, gray)))
        check("negative-origin", document("/MediaBox [-18 -12 0 0]",
              stream="0.5 g -18 -12 18 12 re f 1 0 0 rg -18 -12 9 6 re f"),
              (24, 16), ((3, 13, red), (20, 2, gray)))
        check("cropbox", document("/MediaBox [0 0 30 24] /CropBox [6 3 24 15]"),
              (24, 16), ((1, 14, red), (20, 2, gray)))
        check("inherited-cropbox", document("/MediaBox [0 0 30 24]",
              "/CropBox [6 3 24 15]"), (24, 16), ((1, 14, red),))
        check("cropbox-intersection", document("/MediaBox [0 0 18 12] /CropBox [-6 -3 30 24]"),
              (24, 16), ((3, 13, red),))
        check("indirect-boxes-and-rotation", document("/MediaBox 7 0 R /CropBox 8 0 R /Rotate 9 0 R",
              extra={7: "[0 0 30 24]", 8: "[6 3 24 15]", 9: "90"}),
              (16, 24), ((1, 1, red), (13, 20, gray)))
        check("reversed-mediabox", document("/MediaBox [18 12 0 0]"), (24, 16))
        check("nested-dictionary-decoy", document("/Custom << /MediaBox [0 0 90 90] /Rotate 90 >> /MediaBox [0 0 18 12]"), (24, 16))
        check("comment-in-box", document("/MediaBox [0 0 % comment\n18 12]"), (24, 16))
        check("inherited-through-intermediate-node", document(parent_attrs=
              "/MediaBox [0 0 18 12]", extra={7: "<< /Type /Pages /Parent 2 0 R /Rotate 90 >>"}, page_parent=7), (16, 24))
        check("name-value-decoy", document("/Custom /MediaBox /MediaBox [0 0 18 12]"), (24, 16))
        text = "0.5 g 0 0 36 24 re f 0 g BT /F1 9 Tf 3 3 Td (A) Tj ET"
        original = check("text-unrotated", document("/MediaBox [0 0 36 24]", stream=text), (48, 32))
        for angle in (90, 180, 270):
            width, height = (48, 32) if angle == 180 else (32, 48)
            rotated = check(f"text-rotated-{angle}", document(
                f"/MediaBox [0 0 36 24] /Rotate {angle}", stream=text), (width, height))
            if original and rotated:
                expected = bytearray(width * height * 4)
                for y in range(32):
                    for x in range(48):
                        dx, dy = ((31 - y, x) if angle == 90 else
                                  (47 - x, 31 - y) if angle == 180 else (y, 47 - x))
                        expected[(dy * width + dx) * 4:(dy * width + dx + 1) * 4] = original[2][(y * 48 + x) * 4:(y * 48 + x + 1) * 4]
                if rotated[2] != expected:
                    failures += 1
                    print("FAIL rotated text pixels", angle, file=sys.stderr)
        check("second-page", document(parent_attrs="/MediaBox [0 0 18 12]", second=True),
              (12, 24), page=1, pages=2)
        check("page-out-of-range", document("/MediaBox [0 0 18 12]"), page=1, reject=True)
        for attrs in ("/MediaBox [0 0 0 12]", "/MediaBox [0 0 99999999999999999999 12]",
                      "/MediaBox [0 0 100000 100000]", "/MediaBox [0 0 18]",
                      "/MediaBox [0 0 18 12] /Rotate 45",
                      "/MediaBox [0 0 18 12] /CropBox [30 30 40 40]",
                      "/MediaBox 99 0 R"):
            check("invalid-" + str(count), document(attrs), reject=True)
        check("parent-cycle", document(extra={2: "<< /Type /Pages /Count 1 /Kids [3 0 R] /Parent 2 0 R >>"}), reject=True)
    print(f"PDF page regressions: {count - failures}/{count} passed")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main())
