#!/usr/bin/env python3
"""Owned glyph instructions for legacy/modern TrueType page controls."""

import struct
from test_pdf_subset_fonts import FONT, repack
from test_pdf_truetype import tables
from test_pdf_tt_policy import names
from test_pdf_embedded_fonts import fixture as page

# One touched point translates the contour through IUP. The modern grayscale
# profile ignores the horizontal adjustment and retains the vertical one.
X = bytes((0x01, 0xB0, 0, 0xB8, 0, 96, 0x48, 0x31))
Y = bytes((0x00, 0xB0, 0, 0xB8, 0, 64, 0x48, 0x30))


def font(control=False, full=False):
    mapping = tables(FONT)

    def payload(tag):
        _, at, length = mapping[tag]
        return FONT[at : at + length]

    head = bytearray(payload(b"head"))
    maxp = bytearray(payload(b"maxp"))
    glyph_count = struct.unpack_from(">H", maxp, 4)[0]
    short = struct.unpack_from(">h", head, 50)[0] == 0
    location = payload(b"loca")
    width = 2 if short else 4
    offsets = [
        int.from_bytes(location[i * width : (i + 1) * width], "big") * (2 if short else 1)
        for i in range(glyph_count + 1)
    ]
    glyf = payload(b"glyf")
    output = bytearray()
    locations = [0]
    program = Y if control else X + Y
    for glyph in range(glyph_count):
        record = glyf[offsets[glyph] : offsets[glyph + 1]]
        if glyph == 1:
            contours = struct.unpack_from(">h", record)[0]
            assert contours > 0
            at = 10 + 2 * contours
            old = struct.unpack_from(">H", record, at)[0]
            record = (
                record[:at] + struct.pack(">H", len(program)) + program + record[at + 2 + old :]
            )
        output += record
        output += bytes(len(output) % 2)
        locations.append(len(output))
    struct.pack_into(">h", head, 50, 1)
    struct.pack_into(">H", maxp, 24, max(16, struct.unpack_from(">H", maxp, 24)[0]))
    struct.pack_into(">H", maxp, 26, max(len(program), struct.unpack_from(">H", maxp, 26)[0]))
    changes = {
        b"head": bytes(head),
        b"maxp": bytes(maxp),
        b"glyf": bytes(output),
        b"loca": b"".join(struct.pack(">I", value) for value in locations),
    }
    if full:
        changes[b"name"] = names([(3, 1, 0x409, 1, "Owned DFKai-SB profile test")])
    return repack(FONT, changes)


def pdf(content, control=False, full=False, **kwargs):
    return page(content, font_data=font(control, full), **kwargs)
