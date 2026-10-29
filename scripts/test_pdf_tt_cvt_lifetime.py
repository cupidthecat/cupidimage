#!/usr/bin/env python3
"""Check glyph-local font-unit CVT writes and record comparison carry-over."""
import ctypes
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import freetype
except ImportError:
    print('TrueType CVT lifetime: skipped (optional freetype-py unavailable)')
    raise SystemExit(0)

from test_pdf_tt_state_reference import build_font, push


def main():
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(freetype.get_handle(), b'truetype', b'interpreter-version',
                                   ctypes.byref(version)) == 0
    cases = []
    metadata = []
    read = push(0, 0) + b'\x45\x48'
    for baseline in (-97, 0, 64, 257):
        for changed in (-64, 125, 4096):
            for copy_first in (False, True):
                prep = push(0, baseline) + b'\x44'
                first = (prep if copy_first else b'') + push(0, changed) + b'\x70' + read
                cases.append((prep, first, read))
                metadata.append((baseline, changed, copy_first))
    result = subprocess.check_output([sys.argv[1], '--state-programs'],
        input=''.join(':'.join(code.hex() for code in case) + '\n' for case in cases), text=True)
    native = [list(map(int, line.split())) for line in result.splitlines()]
    assert len(native) == len(cases)
    differences = 0
    with tempfile.TemporaryDirectory(prefix='cupid-tt-cvt-lifetime-') as directory:
        for i, (case, (baseline, changed, copy_first)) in enumerate(zip(cases, metadata)):
            expected = [changed,35,100,0,0,100,baseline,35,100,0,0,100]
            assert native[i] == expected, (i, native[i], expected)
            path = Path(directory) / 'state.ttf'
            build_font(case, path)
            face = freetype.Face(str(path))
            face.set_pixel_sizes(1, 1)
            actual = []
            for glyph in (1, 2):
                face.load_glyph(glyph, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT |
                                freetype.FT_LOAD_TARGET_MONO)
                actual.extend(value for point in face.glyph.outline.points for value in point)
            if actual != expected:
                assert not copy_first and actual == [changed,35,100,0,0,100] * 2, (i, actual, expected)
                differences += 1
            del face
    print(f'TrueType CVT lifetime: 24 native restoration checks, {24-differences} independent matches, '
          f'{differences} recorded WCVTF carry-over differences; engine {freetype.version()}')


if __name__ == '__main__':
    main()
