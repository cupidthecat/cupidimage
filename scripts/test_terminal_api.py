#!/usr/bin/env python3
"""Decode emitted Kitty chunks independently and compare raw RGBA bytes."""
import base64
import re
import subprocess
import sys
import zlib

probe = sys.argv[1]
raw = bytes((i * 73 + 19) % 256 for i in range(64 * 32 * 4))


def commands(mode, *args):
    result = subprocess.run([probe, mode, *args], check=True, capture_output=True)
    matches = re.findall(rb"\x1b_G([^\x1b]*)\x1b\\", result.stdout)
    assert matches
    return matches


def payload(chunks):
    encoded = bytearray()
    header = {}
    for i, chunk in enumerate(chunks):
        fields, data = chunk.split(b";", 1)
        control = dict(field.split(b"=", 1) for field in fields.split(b","))
        if i == 0:
            header = control
        assert len(data) <= 4096
        assert control[b"m"] == (b"0" if i == len(chunks) - 1 else b"1")
        assert len(data) % 4 == 0
        encoded += data
    decoded = base64.b64decode(encoded, validate=True)
    if header.get(b"o") == b"z":
        decoded = zlib.decompress(decoded)
    assert decoded == raw
    assert header[b"s"] == b"64" and header[b"v"] == b"32"
    return header


image = payload(commands("image"))
assert image[b"i"] == b"42" and image[b"p"] == b"7"
assert image[b"c"] == b"80" and image[b"r"] == b"24"
assert commands("delete") == [b"a=d,d=i,i=42,p=7"]
for loops, expected in (("0", b"1"), ("1", b"2"), ("3", b"4")):
    emitted = commands("animation", loops)
    groups, current = [], []
    for command in emitted:
        if b";" not in command:
            continue
        current.append(command)
        if b"m=0;" in command:
            groups.append(current)
            current = []
    assert len(groups) == 2
    payload(groups[0])
    frame = payload(groups[1])
    assert frame[b"a"] == b"f" and frame[b"z"] == b"-1"
    assert emitted[-2] == b"a=a,i=42,r=1,z=20,q=1"
    assert emitted[-1] == b"a=a,i=42,s=3,v=" + expected + b",q=1"
assert subprocess.check_output([probe, "ansi"]) == b"\x1b[48;2;19;92;165m \x1b[0m\n"
print("Terminal streams: RGBA round trips, chunking, deletion, timing, loops, and ANSI passed")
