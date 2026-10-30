#!/usr/bin/env python3
"""Compare authored line, conic and cubic coverage with an optional font engine."""
import ctypes
import random
import subprocess
import sys

try:
    import freetype
except ImportError:
    print('PDF coverage reference: skipped (optional freetype-py unavailable)')
    raise SystemExit(0)


def native_coverage(probe, points, width, height, rule=0):
    request = f'{len(points)} {width} {height} {rule}\n'
    request += ''.join(f'{x} {y} {tag} {end}\n' for x, y, tag, end in points)
    result = subprocess.run([probe, '--probe'], input=request.encode(), capture_output=True, timeout=10)
    assert result.returncode == 0, (result.returncode, result.stderr, request)
    assert len(result.stdout) == width * height
    return result.stdout


def reference_coverage(points, width, height, rule=0):
    count = len(points)
    vectors = (freetype.FT_Vector * count)(*[freetype.FT_Vector(x, y) for x, y, _, _ in points])
    tags = (ctypes.c_ubyte * count)(*[tag for _, _, tag, _ in points])
    indices = [index for index, point in enumerate(points) if point[3]]
    ends = (ctypes.c_short * len(indices))(*indices)
    outline = freetype.FT_Outline(len(indices), count, vectors, tags, ends, 2 if rule else 0)
    buffer = (ctypes.c_ubyte * (width * height))()
    bitmap = freetype.FT_Bitmap(height, width, -width, buffer, 256, 2, b'\0', None)
    error = freetype.FT_Outline_Get_Bitmap(freetype.get_handle(), ctypes.byref(outline),
                                         ctypes.byref(bitmap))
    assert error == 0, error
    return bytes(buffer)


def main():
    probe = sys.argv[1]
    rng = random.Random(43019)
    for case in range(600):
        count = rng.randrange(3, 20)
        points = [(rng.randrange(-640, 2688), rng.randrange(-640, 2688), 1, 0)
                  for _ in range(count)]
        if case % 3 == 1:
            points = [(x, y, 0 if i % 2 or case % 4 == 1 else 1, end)
                      for i, (x, y, tag, end) in enumerate(points)]
        if case % 3 == 2:
            count = (count // 3) * 3 + 1
            points = [(rng.randrange(-640, 2688), rng.randrange(-640, 2688),
                       1 if i % 3 == 0 else 2, 0) for i in range(count)]
        if case % 3 != 2 and case % 5 == 0:
            points = [(x, y, tag, int(i % 4 == 3))
                      for i, (x, y, tag, end) in enumerate(points)]
        points[-1] = (*points[-1][:3], 1)
        rule = case % 2
        actual = native_coverage(probe, points, 32, 32, rule)
        expected = reference_coverage(points, 32, 32, rule)
        assert actual == expected, (case, points, sum(a != b for a, b in zip(actual, expected)))
    print(f'PDF coverage reference: 600 exact bitmap matches; engine {freetype.version()}')


if __name__ == '__main__':
    main()
