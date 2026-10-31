#!/usr/bin/env python3
"""Owned JBIG2 test patterns and standard-template arithmetic decisions."""

import struct
import subprocess

TEMPLATES = [
    [
        (-1, 0),
        (-2, 0),
        (-3, 0),
        (-4, 0),
        (3, -1),
        (2, -1),
        (1, -1),
        (0, -1),
        (-1, -1),
        (-2, -1),
        (-3, -1),
        (2, -2),
        (1, -2),
        (0, -2),
        (-1, -2),
        (-2, -2),
    ],
    [
        (-1, 0),
        (-2, 0),
        (-3, 0),
        (3, -1),
        (2, -1),
        (1, -1),
        (0, -1),
        (-1, -1),
        (-2, -1),
        (2, -2),
        (1, -2),
        (0, -2),
        (-1, -2),
    ],
    [(-1, 0), (-2, 0), (2, -1), (1, -1), (0, -1), (-1, -1), (-2, -1), (1, -2), (0, -2), (-1, -2)],
    [(-1, 0), (-2, 0), (-3, 0), (-4, 0), (2, -1), (1, -1), (0, -1), (-1, -1), (-2, -1), (-3, -1)],
]
AT_BITS = [[4, 10, 11, 15], [3], [2], [4]]
EXTENDED_BITS = [1, 13, 9, 14, 12, 5, 2, 3, 11, 4, 15, 10]
PREDICTOR = [0x9B25, 0x795, 0xE5, 0x195]


def parameters(width, height, template=0, extended=False, typical=False, at=None):
    indices = EXTENDED_BITS if extended else AT_BITS[template]
    return {
        "width": width,
        "height": height,
        "template": template,
        "extended": int(extended),
        "typical": int(typical),
        "at": at if at is not None else [TEMPLATES[template][i] for i in indices],
    }


def packed(pixels):
    height = len(pixels)
    width = len(pixels[0]) if height else 0
    stride = (width + 7) // 8
    output = bytearray(stride * height)
    for y, row in enumerate(pixels):
        for x, value in enumerate(row):
            output[y * stride + x // 8] |= int(bool(value)) << (7 - x % 8)
    return bytes(output)


def generic_decisions(pixels, params, skip=None):
    width, height = params["width"], params["height"]
    template = params["template"]
    neighbors = list(TEMPLATES[template])
    indices = EXTENDED_BITS if params["extended"] else AT_BITS[template]
    for index, position in zip(indices, params["at"]):
        neighbors[index] = position

    def pixel(x, y):
        return pixels[y][x] if 0 <= x < width and 0 <= y < height else 0

    events = []
    previous_typical = 0
    for y in range(height):
        typical = 0
        if params["typical"]:
            previous = pixels[y - 1] if y else [0] * width
            typical = int(pixels[y] == previous)
            events.append((PREDICTOR[template], typical ^ previous_typical))
            previous_typical = typical
        if typical:
            continue
        for x in range(width):
            if skip is not None and skip[y][x]:
                assert not pixels[y][x]
                continue
            context = sum(pixel(x + dx, y + dy) << bit for bit, (dx, dy) in enumerate(neighbors))
            events.append((context, pixels[y][x]))
    return 1 << len(neighbors), events


def encode_many(encoder, requests):
    text = []
    for contexts, events in requests:
        text.append(f"{contexts} {len(events)}\n")
        text.extend(f"{context} {value}\n" for context, value in events)
    run = subprocess.run(
        [str(encoder), "--batch"],
        input="".join(text).encode(),
        capture_output=True,
        timeout=120,
        check=True,
    )
    offset = 0
    streams = []
    for _ in requests:
        length = struct.unpack_from(">I", run.stdout, offset)[0]
        offset += 4
        streams.append(run.stdout[offset : offset + length])
        offset += length
    assert offset == len(run.stdout)
    return streams


def segment(number, kind, data, page=1, refs=()):
    assert len(refs) < 5 and all(ref < number for ref in refs)
    flags = (len(refs) << 5) | ((1 << (len(refs) + 1)) - 1)
    size = 1 if number <= 256 else 2 if number <= 65536 else 4
    return (
        struct.pack(">IBB", number, kind, flags)
        + b"".join(ref.to_bytes(size, "big") for ref in refs)
        + bytes([page])
        + struct.pack(">I", len(data))
        + data
    )


def generic_stream(params, encoded):
    width, height = params["width"], params["height"]
    page = struct.pack(">4IBH", width, height, 0, 0, 0, 0)
    region = struct.pack(">4IB", width, height, 0, 0, 0)
    flags = params["template"] * 2 + params["typical"] * 8 + params["extended"] * 16
    attributes = bytes([flags]) + b"".join(struct.pack("bb", x, y) for x, y in params["at"])
    return segment(1, 48, page) + segment(2, 38, region + attributes + encoded)


def refinement_decisions(pixels, reference, params):
    template, typical = params["template"], params["typical"]
    width, height = params["width"], params["height"]
    rw, rh = len(reference[0]), len(reference)
    dx, dy = params["dx"], params["dy"]
    output_neighbors = [(-1, -1), (0, -1), (1, -1), (-1, 0)]
    reference_neighbors = (
        [(x, y) for y in range(-1, 2) for x in range(-1, 2)]
        if template == 0
        else [(0, -1), (-1, 0), (0, 0), (1, 0), (0, 1), (1, 1)]
    )
    if template == 0:
        output_neighbors[0], reference_neighbors[0] = params["at"]

    def pixel(x, y):
        return pixels[y][x] if 0 <= x < width and 0 <= y < height else 0

    def ref(x, y):
        return reference[y][x] if 0 <= x < rw and 0 <= y < rh else 0

    events, previous = [], 0
    for y in range(height):
        implicit = []
        for x in range(width):
            value = ref(x - dx, y - dy)
            implicit.append(
                all(
                    ref(x - dx + ox, y - dy + oy) == value
                    for oy in range(-1, 2)
                    for ox in range(-1, 2)
                )
            )
        ltp = int(
            typical
            and all(not implicit[x] or pixels[y][x] == ref(x - dx, y - dy) for x in range(width))
        )
        if typical:
            events.append((8 if template else 16, ltp ^ previous))
            previous = ltp
        for x in range(width):
            if ltp and implicit[x]:
                continue
            values = [pixel(x + ox, y + oy) for ox, oy in output_neighbors]
            values += [ref(x - dx + ox, y - dy + oy) for ox, oy in reference_neighbors]
            context = 0
            for bit in values:
                context = (context << 1) | bit
            events.append((context, pixels[y][x]))
    return (1024 if template else 8192), events


def refinement_stream(params, reference, encoded_reference, encoded):
    assert params["dx"] == params["dy"] == 0
    width, height = params["width"], params["height"]
    page = struct.pack(">4IBH", width, height, 0, 0, 0, 0)
    generic = struct.pack(">4IB", len(reference[0]), len(reference), 0, 0, 0)
    generic += b"\x00\x03\xff\xfd\xff\x02\xfe\xfe\xfe" + encoded_reference
    region = struct.pack(">4IB", width, height, 0, 0, 0)
    flags = params["template"] + 2 * params["typical"]
    attributes = bytes([flags])
    if not params["template"]:
        attributes += b"".join(struct.pack("bb", x, y) for x, y in params["at"])
    return (
        segment(1, 48, page)
        + segment(2, 36, generic)
        + segment(3, 42, region + attributes + encoded, refs=(2,))
    )


INTEGER_NAMES = ["DH", "DW", "EX", "AI", "DT", "FS", "DS", "IT", "RI", "RDW", "RDH", "RDX", "RDY"]


def integer_decisions(name, value):
    sign = int(value is None or value < 0)
    magnitude = abs(value) if value is not None else 0
    offsets = [0, 4, 20, 84, 340, 4436]
    widths = [2, 4, 6, 8, 12, 32]
    level = max(i for i, offset in enumerate(offsets) if magnitude >= offset)
    magnitude -= offsets[level]
    bits = [sign] + [1] * level + ([0] if level < 5 else [])
    bits += [(magnitude >> i) & 1 for i in reversed(range(widths[level]))]
    previous, output = 1, []
    base = INTEGER_NAMES.index(name) * 512
    for bit in bits:
        output.append((base + previous, bit))
        previous = ((previous * 2 + bit) & 511) | (256 if previous >= 256 else 0)
    return output


def identifier_decisions(value, length, base=8192):
    previous, events = 1, []
    for i in reversed(range(length)):
        bit = (value >> i) & 1
        events.append((base + previous, bit))
        previous = previous * 2 + bit
    return events


def dictionary_decisions(symbols):
    events = integer_decisions("DH", len(symbols[0]))
    previous_width = 0
    for symbol in symbols:
        width = len(symbol[0])
        events += integer_decisions("DW", width - previous_width)
        _, bitmap = generic_decisions(symbol, parameters(width, len(symbol)))
        events += [(context + 16384, bit) for context, bit in bitmap]
        previous_width = width
    events += integer_decisions("DW", None)
    events += integer_decisions("EX", 0) + integer_decisions("EX", len(symbols))
    return 81920, events


def dictionary_segment(symbols, encoded, number=2):
    attributes = b"\0\0\x03\xff\xfd\xff\x02\xfe\xfe\xfe"
    data = attributes + struct.pack(">II", len(symbols), len(symbols)) + encoded
    return segment(number, 0, data)


def bit_bytes(bits):
    return bytes(int((bits[i : i + 8] + "0000000")[:8], 2) for i in range(0, len(bits), 8))


def standard_huffman(number):
    import re
    from pathlib import Path

    text = (
        Path(__file__).resolve().parents[1] / "third_party/jbig2-tables/huffman.inc"
    ).read_text()
    rows = [
        tuple(map(int, match)) for match in re.findall(r"\{(\d+), (\d+), (\d+), (-?\d+)\}", text)
    ]
    offsets = list(map(int, re.search(r"offsets\[16\] = \{([^}]+)", text).group(1).split(",")))
    selected = rows[offsets[number - 1] : offsets[number]]
    codes, code, previous = {}, 0, 0
    for index in sorted(range(len(selected)), key=lambda i: (selected[i][0], i)):
        prefix, width, flags, low = selected[index]
        if not prefix:
            continue
        code <<= prefix - previous
        codes[index] = f"{code:0{prefix}b}"
        previous = prefix
        code += 1

    def encode(value):
        for index, (prefix, width, flags, low) in enumerate(selected):
            if flags == 2:
                if value is None:
                    return codes[index]
                continue
            if value is None:
                continue
            extra = low - value if flags == 1 else value - low
            if 0 <= extra < (1 << width):
                return codes[index] + (f"{extra:0{width}b}" if width else "")
        raise ValueError((number, value))

    return encode


def check_embedded(path, expected, width, height, file_decoder, reference_adapter, label):
    """Check PDF sample polarity separately from an optional independent bitmap oracle."""
    if file_decoder:
        result = subprocess.run(
            [str(file_decoder.resolve()), str(path), str(width), str(height)],
            capture_output=True,
            timeout=15,
        )
        assert result.returncode == 0, (label, "embedded decode failed", result.stderr)
        assert result.stdout == bytes(value ^ 255 for value in expected), (
            label,
            "PDF samples differ",
        )
    if not reference_adapter:
        return 0
    import sys

    result = subprocess.run(
        [sys.executable, str(reference_adapter.resolve()), str(path)],
        capture_output=True,
        timeout=15,
    )
    assert result.returncode == 0, (label, "independent decode failed", result.stderr)
    w, h, stride = struct.unpack("=3I", result.stdout[:12])
    assert (w, h) == (width, height) and len(result.stdout) == 12 + stride * height
    actual = bytearray()
    for y in range(height):
        row = bytearray(result.stdout[12 + y * stride : 12 + y * stride + (width + 7) // 8])
        if width % 8:
            row[-1] &= (255 << (8 - width % 8)) & 255
        actual += row
    assert actual == expected, (label, "independent bitmap differs")
    return 1
