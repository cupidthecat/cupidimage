#!/usr/bin/env python3
"""Generate small JPEGs and check decoder results without external codecs."""

import pathlib
import struct
import subprocess
import sys
import tempfile


def segment(marker, payload):
    return bytes((255, marker)) + struct.pack(">H", len(payload) + 2) + payload


def frame(components=1, progressive=False, width=8, height=8, ids=None, sampling=None):
    ids = ids or list(range(1, components + 1))
    payload = struct.pack(">BHHB", 8, height, width, components)
    sampling = sampling or [0x11] * components
    payload += b"".join(bytes((i, hv, 0)) for i, hv in zip(ids, sampling))
    return segment(0xC2 if progressive else 0xC0, payload)


def huffman(dc=0, ac=0):
    counts = bytes((1,)) + bytes(15)
    return segment(0xC4, b"\0" + counts + bytes((dc,)) + b"\x10" + counts + bytes((ac,)))


def scan(ids=(1,), ss=0, se=63, ah=0, al=0, selectors=None):
    selectors = selectors or [0] * len(ids)
    return segment(0xDA, bytes((len(ids),)) + b"".join(
        bytes((i, t)) for i, t in zip(ids, selectors)) + bytes((ss, se, ah * 16 + al)))


SOI = b"\xff\xd8"
EOI = b"\xff\xd9"
QT = segment(0xDB, b"\0" + bytes((8,)) * 64)


def jpeg(components=1, progressive=False, width=8, height=8, dc=0):
    return SOI + QT + frame(components, progressive, width, height) + huffman(dc)


def main():
    probe = pathlib.Path(sys.argv[1]).resolve()
    cases = []

    def add(name, data, accepted=True, width=8, height=8, rgb=(128, 128, 128)):
        cases.append((name, data, int(accepted), width, height, *rgb))

    # T.81 Annex G: a ZRL counts zero coefficients within the spectral band.
    for se in (15, 16):
        data = jpeg(progressive=True) + scan(se=0) + b"\x7f"
        add(f"first-ac-zrl-band-{se}", data + huffman(ac=0xF0) +
            scan(ss=1, se=se) + b"\x7f" + EOI, se == 16)
        data += scan(ss=1, se=se, al=1) + b"\x7f"
        add(f"refine-ac-zrl-band-{se}", data + huffman(ac=0xF0) +
            scan(ss=1, se=se, ah=1) + b"\x7f" + EOI, se == 16)
    data = jpeg(progressive=True) + scan(se=0) + b"\x7f"
    data += scan(ss=1, se=15, al=1) + b"\x7f"
    add("refine-new-coefficient-past-band", data + huffman(ac=0xF1) +
        scan(ss=1, se=15, ah=1) + b"\x7f" + EOI, False)

    base = jpeg()
    add("baseline-gray", base + scan() + b"\x3f" + EOI)
    add("baseline-rgb", jpeg(3) + scan((1, 2, 3)) + b"\x03" + EOI)
    add("baseline-rgb-components", jpeg(3, dc=1) + scan((1, 2, 3)) + b"\x41\x7f" + EOI,
        rgb=(130, 129, 127))
    add("reordered-scan-components", jpeg(3, dc=1) + scan((3, 1, 2)) + b"\x48\x7f" + EOI,
        rgb=(130, 129, 127))
    add("baseline-cmyk", jpeg(4) + scan((1, 2, 3, 4)) + b"\0" + EOI, rgb=(0, 0, 0))
    add("progressive-cmyk", jpeg(4, True) + scan((1, 2, 3, 4), se=0) + b"\x0f" + EOI,
        rgb=(0, 0, 0))
    add("progressive-gray", jpeg(progressive=True) + scan(se=0) + b"\x7f" + EOI)
    for width, height, dc_bits, y_bits, chroma_bits in (
            (8, 8, b"\x03", b"\x7f", b"\x7f"),
            (17, 9, b"\0\x0f", b"\x03", b"\x3f")):
        data = SOI + QT + frame(3, True, width, height, sampling=[0x22, 0x11, 0x11]) + huffman()
        data += scan((1, 2, 3), se=0) + dc_bits
        data += scan(ss=1) + y_bits + scan((2,), ss=1) + chroma_bits
        data += scan((3,), ss=1) + chroma_bits + EOI
        add(f"progressive-420-{width}x{height}", data, width=width, height=height)
    add("negative-dc-first", jpeg(progressive=True, dc=1) + scan(se=0, al=1) + b"\x3f" + EOI,
        rgb=(126, 126, 126))
    add("negative-dc-refine", jpeg(progressive=True, dc=1) + scan(se=0, al=1) + b"\x3f" +
        scan(se=0, ah=1) + b"\xff\0" + EOI, rgb=(127, 127, 127))
    add("negative-ac-first", jpeg(progressive=True) + scan(se=0) + b"\x7f" +
        huffman(ac=1) + scan(ss=1, se=1, al=1) + b"\x3f" + EOI, rgb=(-1, -1, -1))
    for progressive in (False, True):
        name = "progressive" if progressive else "baseline"
        first = jpeg(progressive=progressive) + segment(0xDD, b"\0\1")
        sos = scan(se=0 if progressive else 63)
        entropy = b"\x7f" if progressive else b"\x3f"
        add(name + "-final-restart-not-required", first + sos + entropy + EOI)
        first = jpeg(progressive=progressive, width=16) + segment(0xDD, b"\0\1")
        add(name + "-restart-between-blocks", first + sos + entropy + b"\xff\xd0" + entropy + EOI,
            width=16)
        add(name + "-wrong-restart", first + sos + entropy + b"\xff\xd1" + entropy + EOI,
            accepted=False)
    for length in range(8):
        payload = bytes((1, 1, 0, 0, 63, 0))[:length]
        if length < 6:
            add("short-sos-" + str(length), base + segment(0xDA, payload), False)
    add("invalid-dc-selector", base + scan(selectors=[0x40]) + b"\x3f" + EOI, False)
    add("invalid-ac-selector", base + scan(selectors=[4]) + b"\x3f" + EOI, False)
    add("duplicate-scan-components", jpeg(3) + scan((1, 1, 3)) + b"\x03" + EOI, False)
    add("duplicate-frame-components", SOI + QT + frame(3, ids=[1, 1, 3]) + huffman() +
        scan((1, 2, 3)) + b"\x03" + EOI, False)
    add("repeated-frame", jpeg(progressive=True) + frame(progressive=True, width=16) +
        scan(se=0) + b"\x3f" + EOI, False)
    add("zero-width", jpeg(width=0) + scan() + b"\x3f" + EOI, False)
    add("zero-height", jpeg(height=0) + scan() + b"\x3f" + EOI, False)
    add("oversized-huffman", SOI + segment(0xC4, b"\0" + bytes((255, 2)) + bytes(14) + bytes(257)), False)
    add("oversubscribed-huffman", SOI + segment(0xC4, b"\0" + bytes((3,)) + bytes(15) + bytes(3)), False)
    add("invalid-dc-category", jpeg(dc=32) + scan() + bytes(16) + EOI, False)
    add("invalid-ac-category", SOI + QT + frame() + huffman(ac=11) + scan() + bytes(16) + EOI, False)
    add("invalid-refinement-step", jpeg(progressive=True) + scan(se=0, ah=3, al=0) + b"\x7f" + EOI, False)

    failures = 0
    with tempfile.TemporaryDirectory(prefix="cupidimage-jpeg-") as directory:
        for name, data, *expected in cases:
            path = pathlib.Path(directory) / (name + ".jpg")
            path.write_bytes(data)
            result = subprocess.run([str(probe), str(path), *map(str, expected)],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if result.returncode:
                failures += 1
                print("FAIL", name, result.stderr[:2500], file=sys.stderr)
    print(f"JPEG regressions: {len(cases) - failures}/{len(cases)} passed")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main())
