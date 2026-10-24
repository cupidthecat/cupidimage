#!/usr/bin/env python3
"""Regenerate packed TIFF fixtures without optional packints codecs."""
from pathlib import Path
import struct


def packed_tiff(bits, photometric):
    width = height = 64
    colors = 1 << bits
    pixels = bytes(sum(((x + k + y) % colors) << (8 - bits * (k + 1))
                       for k in range(8 // bits))
                   for y in range(height) for x in range(0, width, 8 // bits))
    tags = [(256, 4, 1, width), (257, 4, 1, height), (258, 3, 1, bits),
            (259, 3, 1, 1), (262, 3, 1, photometric), (273, 4, 1, 0),
            (277, 3, 1, 1), (278, 4, 1, height), (279, 4, 1, len(pixels))]
    palette = b''
    if photometric == 3:
        tags.append((320, 3, 3 * colors, 0))
        palette = struct.pack('<' + 'H' * (3 * colors),
                              *([i * 65535 // (colors - 1) for i in range(colors)] * 3))
    offset = 8 + 2 + len(tags) * 12 + 4
    entries = bytearray()
    for tag, kind, count, value in tags:
        if tag == 273: value = offset + len(palette)
        if tag == 320: value = offset
        entries.extend(struct.pack('<HHII', tag, kind, count, value))
    return b'II*\0' + struct.pack('<IH', 8, len(tags)) + entries + b'\0' * 4 + palette + pixels


def main():
    root = Path(__file__).resolve().parents[1] / 'assets/tiff'
    for bits in (1, 2, 4):
        (root / f'palette-{bits}bit-ii.tif').write_bytes(packed_tiff(bits, 3))
    for name, photo in [('minisblack', 1), ('miniswhite', 0)]:
        (root / f'bilevel-1bit-{name}-ii.tif').write_bytes(packed_tiff(1, photo))
    for path in root.glob('*.SKIPPED.txt'):
        if path.name.startswith(('palette-', 'bilevel-')):
            path.unlink()
    (root / 'MANIFEST.txt').write_text('\n'.join(sorted(p.name for p in root.glob('*.tif'))) + '\n')


if __name__ == '__main__':
    main()
