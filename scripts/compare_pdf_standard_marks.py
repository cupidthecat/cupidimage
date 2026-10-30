#!/usr/bin/env python3
"""Compare native spacing-mark curves with independently decoded font geometry."""
import argparse
import hashlib
import io
import json
import math
import subprocess
from pathlib import Path

try:
    from fontTools.cffLib import CFFFontSet
    from fontTools.ttLib import TTFont
    from fontTools.pens.recordingPen import RecordingPen
except ImportError:
    print('Standard spacing geometry: skipped (optional fontTools unavailable)')
    raise SystemExit(0)

from generate_pdf_standard_marks import SOURCE


def segments(commands):
    result = []
    start = None
    for command, points in commands:
        if command == 'closePath':
            kind, points = 3, (start,)
        else:
            kind = {'moveTo': 0, 'lineTo': 1, 'curveTo': 2}[command]
        if kind == 0:
            start = points[0]
        values = tuple(value for point in points for value in point)
        result.append((kind, *values, *([0] * (6 - len(values)))))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('probe', type=Path)
    parser.add_argument('--sources', type=Path)
    options = parser.parse_args()
    manifest = json.loads((SOURCE / 'manifest.json').read_text())
    original_matches = 0
    for row in manifest['fonts']:
        path = SOURCE / row['file']
        font = CFFFontSet()
        font.decompile(io.BytesIO(path.read_bytes()), TTFont())
        top = font.topDictIndex[0]
        pen = RecordingPen()
        top.CharStrings['space_uni0326'].draw(pen)
        expected = segments(pen.value)
        assert top.FontMatrix == [.001, 0, 0, .001, 0, 0]
        lines = subprocess.check_output([str(options.probe.resolve()), str(path), '--outline'], text=True).splitlines()
        active = False
        actual = []
        for line in lines:
            fields = line.split()
            if fields[0] == 'outline':
                active = fields[1] == '1'
                if active:
                    assert float(fields[2]) == top.CharStrings['space_uni0326'].width
            elif active and fields[0] == 'segment':
                actual.append(tuple(map(float, fields[1:])))
        assert len(actual) == len(expected), row['standard_name']
        assert all(math.isclose(a, b, abs_tol=1e-9) for aa, bb in zip(actual, expected)
                   for a, b in zip(aa, bb)), row['standard_name']
        if options.sources:
            original = options.sources / row['original_file']
            assert hashlib.sha256(original.read_bytes()).hexdigest() == row['original_sha256']
            with TTFont(original) as original_font:
                original_pen = RecordingPen()
                original_font.getGlyphSet()['space_uni0326'].draw(original_pen)
                assert original_pen.value == pen.value, row['standard_name']
                original_matches += 1
    print(f'Standard spacing geometry: 8 native curve/width matches; {original_matches} original-outline matches')


if __name__ == '__main__':
    main()
