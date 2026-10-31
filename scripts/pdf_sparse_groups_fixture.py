#!/usr/bin/env python3
"""Owned nested Type 3 glyphs with equivalent flattened leaf programs."""

from test_pdf_pages import document
from test_pdf_type3 import stream


def nested(
    depth=4,
    size=(600, 840),
    alpha=1,
    space="/DeviceRGB",
    knockout=False,
    control=False,
    cyclic=False,
    caption=False,
):
    objects = {}
    leaf = "600 0 d0 0.1 .3 .7 rg 50 w 0 0 400 600 re B"
    for level in range(depth):
        font = 10 + level * 2
        following = font + 2
        last = (level == depth - 1 and not cyclic) or control
        if level == depth - 1 and cyclic:
            following = 10
        body = leaf if last else "600 0 d0 BT /Next 1 Tf 0 0 Td (A) Tj ET"
        resources = "" if last else f"/Resources << /Font << /Next {following} 0 R >> >>"
        matrix = ".001 0 0 .001 0 0" if level == 0 else "1 0 0 1 0 0"
        objects[font] = (
            f"<< /Type /Font /Subtype /Type3 /FontBBox [-20 -20 500 700] "
            f"/FontMatrix [{matrix}] /FirstChar 65 /LastChar 65 /Widths [600] "
            f"/Encoding << /Differences [65 /A] >> /CharProcs << /A {font+1} 0 R >> {resources} >>"
        )
        objects[font + 1] = stream(body)
    if caption:
        objects[1000] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
    caption_font = "/Label 1000 0 R" if caption else ""
    caption_text = (
        " 0 g BT /Label 18 Tf 36 780 Td (Six nested Type 3 glyph groups) Tj ET "
        "BT /Label 11 Tf 36 760 Td (The small drawing retains full-page backdrop semantics.) Tj ET"
        if caption
        else ""
    )
    group = f"/Group << /S /Transparency /I true /K {str(knockout).lower()} /CS {space} >>"
    return document(
        f"/MediaBox [0 0 {size[0]} {size[1]}] {group} /Resources << /Font << /F 10 0 R {caption_font} >> "
        f"/ExtGState << /A << /ca {alpha} /CA {alpha} >> >> >>",
        stream="1 .9 .6 rg 0 0 100 100 re f /A gs BT /F 36 Tf 40 40 Td (A) Tj ET" + caption_text,
        extra=objects,
    )
