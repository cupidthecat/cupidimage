#!/usr/bin/env python3
"""Compare native composite glyph execution with pinned-font outline results."""
import argparse
import ctypes
import subprocess

try:
    import freetype
    from fontTools.ttLib import TTFont
except ImportError:
    print('TrueType composite reference: skipped (optional fontTools/freetype-py unavailable)')
    raise SystemExit(0)

from generate_pdf_extended_fonts import FONTS, SOURCE

from pdf_tt_reference import outline_state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe')
    parser.add_argument('--all', action='store_true')
    parser.add_argument('--gray', action='store_true')
    options = parser.parse_args()
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(freetype.get_handle(), b'truetype', b'interpreter-version',
                                   ctypes.byref(version)) == 0
    sizes = (16,) if options.all else (6, 9, 13, 16, 24, 37)
    checked = 0
    failures = []
    for filename in FONTS.values():
        path = SOURCE / filename
        with TTFont(path) as font:
            names = (font.getGlyphOrder() if options.all else
                     [name for code, name in font.getBestCmap().items() if 32 <= code < 256])
            names = [name for name in names if font['glyf'][name].isComposite()]
            face = freetype.Face(str(path))
            for ppem in sizes:
                face.set_pixel_sizes(ppem, ppem)
                for name in names:
                    index = font.getGlyphID(name)
                    result = subprocess.run([options.probe, str(path), '--native-gray' if options.gray else '--native', str(ppem), str(index)],
                                            capture_output=True, text=True, timeout=20)
                    assert result.returncode == 0, (filename, name, ppem, result.stderr)
                    lines = result.stdout.splitlines()
                    status = list(map(int, lines[0].split()))
                    if status[:4] != [1, 0, 0, 0]:
                        failures.append((filename, name, ppem, 'execution', status))
                        continue
                    native = [tuple(map(int, line.split())) for line in lines[1:]]
                    face.load_glyph(index, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT |
                                    (freetype.FT_LOAD_TARGET_NORMAL if options.gray else freetype.FT_LOAD_TARGET_MONO))
                    actual = outline_state(face)
                    if native != actual:
                        failures.append((filename, name, ppem, 'points',
                                         [(i, a, b) for i, (a, b) in enumerate(zip(native, actual)) if a != b][:4]))
                    checked += 1
            del face
    assert not failures, (len(failures), failures[:30])
    print(f'TrueType composite reference: {checked} outlines at {len(sizes)} sizes, 0 failures; '
          f'engine {freetype.version()}; {"grayscale" if options.gray else "monochrome"}; '
          'native assembly and instructions, no PDF rendering')


if __name__ == '__main__':
    main()
