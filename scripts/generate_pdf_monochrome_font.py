#!/usr/bin/env python3
"""Build owned narrow-stem glyphs with independently selectable scan policies."""

import base64
import io
import json
from pathlib import Path
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables._g_l_y_f import Glyph, GlyphComponent
from fontTools.ttLib.tables.ttProgram import Program
from generate_pdf_page_hinting_font import push


def build(control=False):
    policies = [(0x1FF, mode) for mode in range(8)]
    policies += [(0, 0), (0x1210, 0), (0x2410, 0), (0x1010, 0), (0x2010, 0), (0x0010, 0)]
    names = [chr(65 + i) for i in range(len(policies))] + ["Q", "R"]
    glyphs = {".notdef": TTGlyphPen(None).glyph()}
    for name, (flags, mode) in zip(names, policies):
        pen = TTGlyphPen(None)
        pen.moveTo((17, 0))
        pen.lineTo((21, 0))
        pen.lineTo((21, 128))
        pen.lineTo((17, 128))
        pen.closePath()
        glyph = pen.glyph()
        glyph.program = Program()
        code = push(0x1FF) + b"\x85" + push(flags) + b"\x85" + push(mode) + b"\x8d"
        glyph.program.fromBytecode(b"" if control else code)
        glyphs[name] = glyph
    for name in ("Q", "R"):
        glyph = Glyph()
        glyph.numberOfContours = -1
        glyph.components = []
        for child, offset in (("A", 0), ("E", 128)):
            component = GlyphComponent()
            component.glyphName = child
            component.x = offset
            component.y = 0
            component.flags = 0x1000
            glyph.components.append(component)
        if name == "R":
            glyph.program = Program()
            glyph.program.fromBytecode(b"" if control else push(0) + b"\x85" + push(2) + b"\x8d")
        glyphs[name] = glyph
    font = FontBuilder(256, isTTF=True)
    font.setupGlyphOrder([".notdef"] + names)
    font.setupCharacterMap({ord(name): name for name in names})
    font.setupGlyf(glyphs)
    font.setupHorizontalMetrics({name: (256, 17 if name != ".notdef" else 0) for name in glyphs})
    font.setupHorizontalHeader(ascent=256, descent=-64)
    font.setupNameTable(
        {
            "familyName": "CupidImage Scan Policies",
            "styleName": "Regular",
            "uniqueFontIdentifier": "CupidImage Scan Policies",
            "fullName": "CupidImage Scan Policies",
            "psName": "CupidImageScanPolicies",
        }
    )
    font.setupOS2(sTypoAscender=256, sTypoDescender=-64, usWinAscent=256, usWinDescent=64)
    font.setupPost()
    font.setupMaxp()
    font.font["maxp"].maxStackElements = 64
    font.font["maxp"].maxStorage = 1
    font.font.recalcTimestamp = False
    font.font["head"].flags &= ~2
    font.font["head"].created = font.font["head"].modified = 3400000000
    output = io.BytesIO()
    font.save(output)
    return output.getvalue()


if __name__ == "__main__":
    data = {
        "description": "Owned narrow rectangles and composites, with SCANCTRL and SCANTYPE programs; no third-party outlines.",
        "font": base64.b64encode(build()).decode(),
        "control": base64.b64encode(build(True)).decode(),
    }
    target = Path(__file__).resolve().parents[1] / "tests/fixtures/pdf-monochrome-font.json"
    target.write_text(json.dumps(data, indent=2) + "\n")
