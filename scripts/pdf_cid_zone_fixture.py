#!/usr/bin/env python3
"""Owned embedded CID subset with a missing maxZones declaration."""

import struct
import zlib
from test_pdf_type3 import stream
from test_pdf_forms import form
from test_pdf_page_hinting import font_descriptor
from test_pdf_subset_fonts import FONT, repack, tt_pair
from test_pdf_truetype import tables
from test_pdf_embedded_fonts import fixture

PREP = bytes((0xB1, 48, 0, 0xB8, 1, 36, 0x18, 0x85, 0x8D, 0x1D))
TWILIGHT = bytes((0xB0, 0, 0x16, 0xB0, 0, 0x2F))


def font(zones=0, twilight=0, reordered=False, prep=PREP):
    source = tt_pair(True, False)[1] if reordered else FONT
    _, offset, length = tables(source)[b"maxp"]
    maxp = bytearray(source[offset : offset + length])
    struct.pack_into(">H", maxp, 14, zones)
    struct.pack_into(">H", maxp, 16, twilight)
    struct.pack_into(">H", maxp, 24, 16)
    return repack(source, {b"maxp": bytes(maxp), b"prep": prep})


def asset(control=False):
    content = (
        "0.1 .3 .7 rg BT /F 18 Tf 18 45 Td <0001000200040005> Tj ET "
        "0 g BT /Label 8 Tf 8 80 Td (CID subset: missing zone declaration) Tj ET "
        "BT /Label 7 Tf 8 15 Td (Hinted and unhinted outlines retain the same glyph identities.) Tj ET"
    )
    page = (
        "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 220 90] "
        "/Resources << /Font << /F 5 0 R /Label 12 0 R >> >> >>"
    )
    return fixture(
        content,
        cid=True,
        font_data=font(1 if control else 0),
        extra={
            3: page,
            12: "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        },
    )


def local_font_page(control=False):
    data = font(1 if control else 0)
    child = form(
        "BT /Local 18 Tf 25 60 Td <00010002> Tj ET",
        "/BBox [0 0 120 90] /Resources << /Font << /Local 30 0 R >> >>",
    )
    objects = {
        20: child,
        30: "<< /Type /Font /Subtype /Type0 /BaseFont /CupidImageTestShapes /Encoding /Identity-V /DescendantFonts [31 0 R] >>",
        31: "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /CupidImageTestShapes /FontDescriptor 32 0 R /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap /Identity /DW 1000 >>",
        32: font_descriptor(data, "CupidImageTestShapes", 33),
        33: stream(zlib.compress(data), "/Filter /FlateDecode /Length1 " + str(len(data))),
        3: "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /MediaBox [0 0 120 90] /Resources << /Font << /F 5 0 R >> /XObject << /G 20 0 R >> >> >>",
    }
    return fixture(
        "/G Do BT /F 18 Tf 65 30 Td <0001> Tj ET /G Do",
        cid=True,
        font_data=font(1),
        extra=objects,
    )
