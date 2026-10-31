#!/usr/bin/env python3
"""Compare authored profile instructions with an optional independent engine."""

import ctypes, subprocess, tempfile, sys
from pathlib import Path

try:
    import freetype
    from fontTools.ttLib import TTFont
except ImportError:
    print("TT profile reference: skipped (optional freetype-py/fonttools unavailable)")
    raise SystemExit(0)
from test_pdf_tt_points_reference import build_font, push
from pdf_tt_reference import outline_state

programs = [b""]
for axis in (0, 1):
    for before in (b"", bytes([0x30, 0x31])):
        for op in (
            push(0) + bytes([0x2F]),
            push(0, 97) + bytes([0x48]),
            push(0, 16) + bytes([0x38]),
            push(0) + bytes([0x2F]) + push(0, 16) + bytes([0x38]),
        ):
            programs.append(bytes([axis]) + before + op)
    for ctrl in (0, 4):
        programs.append(bytes([axis]) + push(ctrl, 3) + bytes([0x8E]) + push(0, 97) + bytes([0x48]))
    for touched in (False, True):
        programs.append(
            bytes([axis])
            + (push(0) + bytes([0x2F]) if touched else b"")
            + push(3)
            + bytes([0x5F])
            + push(64)
            + bytes([0x5E])
            + push(8, 0, 1)
            + bytes([0x5D])
        )
for bit in (1, 2, 4, 8, 32, 64, 256, 1024, 2048, 4096):
    programs.append(
        bytes([0]) + push(bit) + bytes([0x88, 0x58]) + push(0, 97) + bytes([0x48, 0x59])
    )
for op in (push(0) + bytes([0x80]), push(0, 1) + bytes([0x82])):
    programs.append(bytes([0x30, 0x31]) + op)
checks = 0
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / "font.ttf"
    build_font(programs, path)
    for bearing in (0, 23, -19):
        with TTFont(path) as f:
            for name in f.getGlyphOrder():
                f["hmtx"].metrics[name] = (128, bearing)
            f.save(path)
        for version in (35, 40):
            v = ctypes.c_uint(version)
            assert (
                freetype.FT_Property_Set(
                    freetype.get_handle(), b"truetype", b"interpreter-version", ctypes.byref(v)
                )
                == 0
            )
            face = freetype.Face(str(path))
            for gray in (False, True):
                for ppem in (8, 16, 32, 64):
                    face.set_pixel_sizes(ppem, ppem)
                    for gid in range(1, len(programs) + 1):
                        mode = (
                            "--native"
                            + ("-modern" if version == 40 else "")
                            + ("-gray" if gray else "")
                        )
                        r = subprocess.run(
                            [sys.argv[1], str(path), mode, str(ppem), str(gid)],
                            capture_output=True,
                            text=True,
                            timeout=10,
                        )
                        lines = r.stdout.splitlines()
                        assert r.returncode == 0 and lines[0].startswith("1 0 "), (
                            version,
                            gray,
                            ppem,
                            gid,
                            r.stdout,
                            r.stderr,
                        )
                        face.load_glyph(
                            gid,
                            freetype.FT_LOAD_NO_BITMAP
                            | freetype.FT_LOAD_NO_AUTOHINT
                            | (
                                freetype.FT_LOAD_TARGET_NORMAL
                                if gray
                                else freetype.FT_LOAD_TARGET_MONO
                            ),
                        )
                        actual = [tuple(map(int, s.split())) for s in lines[1:]]
                        expected = outline_state(face)
                        assert actual == expected, (
                            bearing,
                            version,
                            gray,
                            ppem,
                            gid,
                            programs[gid - 1].hex(),
                            actual,
                            expected,
                        )
                        checks += 1
print(checks, "exact owned profile outlines", freetype.version())
