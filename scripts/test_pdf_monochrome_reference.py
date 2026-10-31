#!/usr/bin/env python3
"""Compare owned monochrome outlines with an independent font-engine invocation."""

import argparse
import base64
import ctypes
import json
from pathlib import Path
import zlib
import random
import subprocess
import sys

try:
    import freetype
except ImportError:
    print("Monochrome reference skipped: optional freetype-py unavailable")
    raise SystemExit(0)


HANDLE = None
GET_BITMAP = None
ENGINE_VERSION = None
CUSTOM_LIBRARY = None


def select_library(path):
    global HANDLE, GET_BITMAP, ENGINE_VERSION, CUSTOM_LIBRARY
    if path:
        CUSTOM_LIBRARY = ctypes.CDLL(path)
        HANDLE = freetype.FT_Library()
        initialize = CUSTOM_LIBRARY.FT_Init_FreeType
        initialize.argtypes = [ctypes.POINTER(freetype.FT_Library)]
        initialize.restype = ctypes.c_int
        assert initialize(ctypes.byref(HANDLE)) == 0
        major, minor, patch = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
        version = CUSTOM_LIBRARY.FT_Library_Version
        version.argtypes = [freetype.FT_Library] + [ctypes.POINTER(ctypes.c_int)] * 3
        version.restype = None
        version(HANDLE, ctypes.byref(major), ctypes.byref(minor), ctypes.byref(patch))
        ENGINE_VERSION = (major.value, minor.value, patch.value)
        GET_BITMAP = CUSTOM_LIBRARY.FT_Outline_Get_Bitmap
        GET_BITMAP.argtypes = [
            freetype.FT_Library,
            ctypes.POINTER(freetype.FT_Outline),
            ctypes.POINTER(freetype.FT_Bitmap),
        ]
        GET_BITMAP.restype = ctypes.c_int
    else:
        HANDLE = freetype.get_handle()
        GET_BITMAP = freetype.FT_Outline_Get_Bitmap
        ENGINE_VERSION = freetype.version()


def reference(points, width, height, mode, precision):
    count = len(points)
    vectors = (freetype.FT_Vector * count)(*[freetype.FT_Vector(x, y) for x, y, _, _ in points])
    tags = (ctypes.c_ubyte * count)(*[tag for _, _, tag, _ in points])
    indices = [i for i, p in enumerate(points) if p[3]]
    ends = (ctypes.c_short * len(indices))(*indices)
    flags = (
        (0x100 if precision else 0)
        | (8 if mode & 2 else 0)
        | (16 if mode & 4 else 0)
        | (32 if not mode & 1 else 0)
    )
    outline = freetype.FT_Outline(len(indices), count, vectors, tags, ends, flags)
    stride = (width + 7) // 8
    pixels = (ctypes.c_ubyte * (stride * height))()
    bitmap = freetype.FT_Bitmap(height, width, -stride, pixels, 2, 1, b"\0", None)
    error = GET_BITMAP(HANDLE, ctypes.byref(outline), ctypes.byref(bitmap))
    assert not error, error
    return bytes(
        255 if pixels[y * stride + x // 8] & (0x80 >> (x & 7)) else 0
        for y in range(height)
        for x in range(width)
    )


def native(probe, points, width, height, mode, precision):
    data = f"{len(points)} {width} {height} {mode} {precision}\n"
    data += "".join(f"{x} {y} {tag} {end}\n" for x, y, tag, end in points)
    result = subprocess.run(
        [probe, "--probe"], input=data.encode(), capture_output=True, timeout=10
    )
    assert result.returncode == 0, (result.returncode, result.stderr, data)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe")
    parser.add_argument("--library")
    parser.add_argument("--generate", action="store_true")
    args = parser.parse_args()
    select_library(args.library)
    if ENGINE_VERSION < (2, 14, 3):
        print(
            f"Monochrome reference skipped: engine {ENGINE_VERSION}; this profile needs 2.14.3 or newer"
        )
        return
    probe = args.probe
    rng = random.Random(3200093)
    checks = 0
    cases = []
    for x in range(-32, 129, 8):
        for width in (0, 1, 17, 31, 32, 33, 63, 64):
            cases.append(
                [(x, 0, 1, 0), (x + width, 0, 1, 0), (x + width, 256, 1, 0), (x, 256, 1, 1)]
            )
    for case in range(240):
        count = rng.randrange(3, 20)
        if case % 3 == 2:
            count = count // 3 * 3 + 1
        points = [
            (
                rng.randrange(-128, 1152),
                rng.randrange(-128, 1152),
                1 if case % 3 == 0 else (i % 2 if case % 3 == 1 else (1 if i % 3 == 0 else 2)),
                0,
            )
            for i in range(count)
        ]
        points[-1] = (*points[-1][:3], 1)
        cases.append(points)
    for first_mode in range(8):
        for second_mode in range(8):
            cases.append(
                [
                    (70, 0, 5 | first_mode << 5, 0),
                    (90, 0, 1, 0),
                    (90, 256, 1, 0),
                    (70, 256, 1, 1),
                    (262, 128, 5 | second_mode << 5, 0),
                    (282, 128, 1, 0),
                    (282, 384, 1, 0),
                    (262, 384, 1, 1),
                ]
            )
    records = []
    for index, points in enumerate(cases):
        masks = []
        for mode in range(8):
            precision = index % 2
            actual = native(probe, points, 16, 16, mode, precision)
            expected = reference(points, 16, 16, mode, precision)
            assert actual == expected, (
                index,
                mode,
                points,
                sum(a != b for a, b in zip(actual, expected)),
            )
            checks += 1
            masks.append(base64.b64encode(zlib.compress(expected)).decode())
        records.append(dict(points=points, width=16, height=16, precision=index % 2, masks=masks))
    if args.generate:
        path = Path(__file__).resolve().parents[1] / "tests/fixtures/pdf-monochrome-reference.json"
        path.write_text(
            json.dumps(
                dict(
                    provenance="Owned line, quadratic, cubic and contour-policy outlines",
                    engine=list(ENGINE_VERSION),
                    cases=records,
                ),
                indent=2,
            )
            + "\n"
        )
    print(f"Monochrome reference: {checks} exact bitmap matches; engine {ENGINE_VERSION}")
    if CUSTOM_LIBRARY:
        finish = CUSTOM_LIBRARY.FT_Done_FreeType
        finish.argtypes = [freetype.FT_Library]
        finish.restype = ctypes.c_int
        assert finish(HANDLE) == 0


if __name__ == "__main__":
    main()
