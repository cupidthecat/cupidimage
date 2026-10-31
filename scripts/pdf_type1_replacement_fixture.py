#!/usr/bin/env python3
"""Owned Type 1 outlines with bounded active sets and longer replacement traces."""

from test_pdf_type1 import fixture
from test_pdf_type1_charstrings import WIDTH, command

CASES = ((24, 4), (25, 4), (32, 4), (8, 16), (8, 32), (10, 32), (96, 1), (97, 1))


def program(stems, groups, vertical=False, hints=True):
    code = WIDTH + command([0, 0], 21)
    subrs = []
    directions = ((400, 0), (0, 600), (-400, 0), (0, -600))
    for group in range(groups):
        if hints:
            declarations = b"".join(command([i * 6, 2], 3 if vertical else 1) for i in range(stems))
            if group:
                subrs.append(declarations + b"\x0b")
                code += command([len(subrs) - 1, 1, 3], (12, 16)) + bytes([12, 17, 10])
            else:
                code += declarations
        code += command(directions[group % 4], 5)
    # Finish incomplete rectangles without adding more hints.
    for edge in range(groups % 4, 4) if groups % 4 else ():
        code += command(directions[edge], 5)
    code += b"\x09\x0e"
    return fixture(
        glyphs=[(".notdef", WIDTH + b"\x0e"), ("A", code)],
        subrs=subrs,
        extra=b"/BlueValues [-20 0 600 620] def\n",
    )


def authored_fonts():
    return [
        (f"replacement-{stems}-{groups}-{axis}", program(stems, groups, axis))
        for stems, groups in CASES
        for axis in (False, True)
    ]
