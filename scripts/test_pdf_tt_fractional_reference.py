#!/usr/bin/env python3
"""Check fractional requests, head rounding flags and per-axis font scaling."""
import ctypes
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import freetype
    from fontTools.ttLib import TTFont
except ImportError:
    print('TrueType fractional reference: skipped (optional fontTools/freetype-py unavailable)')
    raise SystemExit(0)

from pdf_tt_reference import outline_state
from test_pdf_tt_points_reference import build_font, push


def main():
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(freetype.get_handle(), b'truetype', b'interpreter-version',
                                   ctypes.byref(version)) == 0
    programs = [b'']
    for axis in (0, 1):
        for index in range(4):
            programs.append(bytes([axis]) + push(0, index) + b'\x45\x48')
            programs.append(bytes([axis]) + push(1, index) + b'\x3f')
        programs.append(bytes([axis]) + push(0) + b'\x4b\x48')
        programs.append(bytes([axis]) + push(1) + b'\xc4')
    sizes = ((528, 1136), (1136, 528), (518, 538), (538, 518), (800, 2360), (1072, 1072))
    checked = 0
    with tempfile.TemporaryDirectory(prefix='cupid-tt-fractional-') as directory:
        path = Path(directory) / 'fractional.ttf'
        for force in (False, True):
            build_font(programs, path)
            with TTFont(path) as font:
                font['head'].flags = (font['head'].flags & ~8) | (8 if force else 0)
                font.save(path)
            for x, y in sizes:
                for i, code in enumerate(programs):
                    lines = subprocess.check_output([sys.argv[1], str(path), '--native-f26-xy',
                                                     str(x), str(y), str(i+1)], text=True).splitlines()
                    assert lines[0].startswith('1 0 0 0 '), (force, x, y, i, lines[0])
                    native = [tuple(map(int, line.split())) for line in lines[1:]]
                    face = freetype.Face(str(path))
                    face.set_char_size(x, y, 72, 72)
                    face.load_glyph(i+1, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT |
                                    freetype.FT_LOAD_TARGET_MONO)
                    assert native == outline_state(face), (force, x, y, i, code.hex(), native, outline_state(face))
                    checked += 1
                    del face
    print(f'TrueType fractional reference: {checked} complete outline matches, head rounding on/off; '
          f'engine {freetype.version()}; no PDF rendering')


if __name__ == '__main__':
    main()
