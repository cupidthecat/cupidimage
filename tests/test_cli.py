#!/usr/bin/env python3
"""Check static redirected previews and interactive SVG playback."""
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve()
checks = 0


def run(path, *options):
    result = subprocess.run(
        [str(binary), "--ansi", *options, str(path), "32", "16"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5, check=True,
    )
    assert result.stdout and b"\x1b[?25l" not in result.stdout
    return result.stdout


with tempfile.TemporaryDirectory(prefix="cupidimage-cli-") as directory:
    directory = Path(directory)
    svg = directory / "animated.svg"
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="8" height="8">'
        '<rect width="4" height="4" fill="red">'
        '<animate attributeName="x" from="0" to="4" dur="1s" repeatCount="indefinite"/>'
        '</rect></svg>', encoding="utf-8",
    )
    for path in [svg, root / "assets/svg/css_keyframes_basic.svg",
                 root / "assets/svg/animated-green-astronaut-helmet.svg"]:
        assert run(path) == run(path, "--svg-time", "0"), path
        checks += 1

    # Two 1x1 frames, red then blue, with an infinite Netscape loop count.
    gif = directory / "animated.gif"
    gif.write_bytes(
        b"GIF89a\x01\x00\x01\x00\x80\x00\x00\xff\x00\x00\x00\x00\xff"
        b"\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00"
        b"\x21\xf9\x04\x00\x01\x00\x00\x00"
        b"\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02\x44\x01\x00"
        b"\x21\xf9\x04\x00\x01\x00\x00\x00"
        b"\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02\x4c\x01\x00\x3b"
    )
    assert run(gif) == b"\x1b[48;2;255;0;0m \x1b[0m\n"
    checks += 1

    # A real terminal still enters playback. Stop only this test's child process.
    master, slave = os.openpty()
    child = subprocess.Popen(
        [str(binary), "--ansi", str(svg), "8", "8"],
        stdout=slave, stderr=subprocess.PIPE,
    )
    os.close(slave)
    try:
        assert select.select([master], [], [], 5)[0], "no interactive output"
        assert b"\x1b[?25l" in os.read(master, 4096), "animation did not start"
        assert child.poll() is None, "interactive animation stopped early"
        checks += 1
    finally:
        child.terminate()
        child.communicate(timeout=5)
        os.close(master)

print(f"CLI regressions: {checks} checks passed")
