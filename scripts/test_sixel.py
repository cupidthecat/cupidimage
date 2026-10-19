#!/usr/bin/env python3
"""Decode the emitted Sixel stream independently and inspect painted pixels."""

import pathlib
import re
import subprocess
import sys
import tempfile


def decode(data):
    text = data.decode("ascii")
    match = re.match("\x1bP([0-9;]*)q", text)
    assert match, "missing Sixel header"
    parameters = [int(p or "0") for p in match[1].split(";")]
    transparent = len(parameters) > 1 and parameters[1] == 1
    position = match.end()
    raster = re.match(r'"\d+;\d+;(\d+);(\d+)', text[position:])
    assert raster
    width, height = map(int, raster.groups())
    pixels = [None] * (width * height)
    palette = {}
    x = y = color = 0
    position += raster.end()
    while not text.startswith("\x1b\\", position):
        char = text[position]
        if char == "#":
            token = re.match(r"#(\d+)(?:;2;(\d+);(\d+);(\d+))?", text[position:])
            assert token
            color = int(token[1])
            if token[2] is not None:
                palette[color] = tuple(map(int, token.groups()[1:]))
            position += token.end()
            continue
        if char == "$":
            x = 0
        elif char == "-":
            x = 0
            y += 6
        else:
            repeat = 1
            if char == "!":
                token = re.match(r"!(\d+)", text[position:])
                repeat = int(token[1])
                position += token.end()
                char = text[position]
            assert "?" <= char <= "~", repr(char)
            bits = ord(char) - 63
            for dx in range(repeat):
                for bit in range(6):
                    if bits & (1 << bit):
                        assert x + dx < width and y + bit < height
                        pixels[(y + bit) * width + x + dx] = palette[color]
            x += repeat
        position += 1
    assert len(palette) <= 256
    return transparent, (width, height), pixels


def main():
    probe = str(pathlib.Path(sys.argv[1]).resolve())
    count = failures = 0
    with tempfile.TemporaryDirectory(prefix="cupidimage-sixel-") as directory:
        def check(name, pixels, width, height, transparency=1, dither=0, colors=256, reject=False, exact=False):
            nonlocal count, failures
            count += 1
            path = pathlib.Path(directory) / (name + ".rgba")
            path.write_bytes(bytes(component for pixel in pixels for component in pixel))
            result = subprocess.run([probe, str(width), str(height), str(transparency), str(dither), str(colors), str(path)], capture_output=True)
            try:
                if reject:
                    assert result.returncode == 3, result.stderr[:1000]
                    assert result.stderr and not result.stdout
                    return
                assert result.returncode == 0, result.stderr[:1000]
                transparent, dimensions, actual = decode(result.stdout)
                assert dimensions == (width, height)
                assert transparent == (transparency > 0), "incorrect background mode"
                for pixel, painted in zip(pixels, actual):
                    visible = transparency <= 0 or pixel[3] >= 128
                    assert (painted is not None) == visible, (pixel, painted)
                    if exact and visible:
                        rgb = pixel[:3] if transparency > 0 else tuple(
                            (c * pixel[3] + 255 * (255 - pixel[3])) // 255 for c in pixel[:3])
                        expected = tuple(c * 100 // 255 for c in rgb)
                        assert painted == expected, (painted, expected)
            except (AssertionError, ValueError, KeyError, IndexError) as error:
                failures += 1
                print("FAIL", name, str(error), file=sys.stderr)

        black, red, clear = (0, 0, 0, 255), (255, 0, 0, 255), (255, 255, 255, 0)
        for dither in range(4):
            check(f"transparent-black-{dither}", [black], 1, 1, dither=dither, colors=2, exact=True)
            check(f"transparent-red-{dither}", [red], 1, 1, dither=dither, colors=2, exact=True)
            check(f"all-transparent-{dither}", [clear] * 8, 4, 2, dither=dither)
            check(f"mixed-alpha-{dither}", [black, red, clear, (0, 0, 0, 127),
                  (0, 0, 0, 128), red, clear, black] * 4, 4, 8, dither=dither)
            check(f"opaque-{dither}", [black, red] * 4, 4, 2, transparency=0, dither=dither, exact=dither != 3)
        check("full-transparent-palette", [(i, 0, 0, 255) for i in range(255)], 255, 1, exact=True)
        check("full-opaque-palette", [(i, 0, 0, 255) for i in range(256)], 256, 1, transparency=0, exact=True)
        check("alpha-blend", [(255, 0, 0, 128)], 1, 1, transparency=0, exact=True)
        check("default-options", [red], 1, 1, transparency=-1, exact=True)
        check("clamp-low-palette-limit", [red, clear], 2, 1, colors=1, exact=True)
        check("clamp-high-palette-limit", [red, clear], 2, 1, colors=999, exact=True)
        check("zero-width", [black], 0, 1, reject=True)
        check("overflow-dimensions", [black], 0xFFFFFFFF, 0xFFFFFFFF, reject=True)
        if pathlib.Path("/dev/full").exists():
            count += 1
            path = pathlib.Path(directory) / "write-error.rgba"
            path.write_bytes(bytes(red))
            with open("/dev/full", "wb") as output:
                result = subprocess.run([probe, "1", "1", "0", "0", "2", str(path)],
                                        stdout=output, stderr=subprocess.PIPE)
            if result.returncode != 3 or b"write Sixel" not in result.stderr:
                failures += 1
                print("FAIL output error", result.stderr[:1000], file=sys.stderr)
    print(f"Sixel regressions: {count - failures}/{count} passed")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main())
