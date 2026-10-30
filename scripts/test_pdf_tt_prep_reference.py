#!/usr/bin/env python3
"""Compare CVT preparation without claiming native glyph execution or rendering.

Temporary fonts retain their notices and are renamed before adding probe glyphs.
The probe clears only glyph inhibition after prep so CVT values can be observed.
"""
import ctypes
import subprocess
import sys
import tempfile
from pathlib import Path

from generate_pdf_extended_fonts import SOURCE, FONTS

try:
    import freetype
    from fontTools.ttLib import TTFont
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib.tables.ttProgram import Program
except ImportError:
    print('TrueType preparation reference: skipped (optional fontTools/freetype-py unavailable)')
    raise SystemExit(0)


SIZES = (1, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 24, 25, 32, 40, 48, 64, 72, 96)


def probe_font(original, path):
    font = TTFont(original)
    for record in font['name'].names:
        if record.nameID in (1, 2, 3, 4, 6):
            name = 'Regular' if record.nameID == 2 else 'CupidVMSizeTest'
            record.string = name.encode(record.getEncoding())
    order = list(font.getGlyphOrder())
    base = len(order)
    count = len(font['cvt '].values)
    for i in range(count):
        name = f'vm_cvt_{i}'
        pen = TTGlyphPen(None)
        pen.moveTo((0, 0))
        pen.lineTo((1000, 0))
        pen.lineTo((0, 1000))
        pen.closePath()
        glyph = pen.glyph()
        glyph.program = Program()
        # SVTCA[x], point 0, CVT index, RCVT, SCFS exposes one prepared value.
        glyph.program.fromBytecode(
            bytes([0x01, 0xb0, 0, 0xb8]) + i.to_bytes(2, 'big') + bytes([0x45, 0x48])
        )
        font['glyf'][name] = glyph
        font['hmtx'].metrics[name] = (1000, 0)
        order.append(name)
    # Some sizes inhibit glyph instructions. Clear that flag only after prep;
    # otherwise a skipped probe would falsely report a zero CVT value.
    program = font['prep'].program
    program.fromBytecode(program.getBytecode() + bytes([0xb1, 0, 1, 0x8e]))
    font.setGlyphOrder(order)
    font.save(path)
    font.close()
    return base, count


def main():
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(
        freetype.get_handle(), b'truetype', b'interpreter-version', ctypes.byref(version)
    ) == 0
    checked = 0
    failures = []
    with tempfile.TemporaryDirectory(prefix='cupid-tt-prep-') as folder:
        for filename in FONTS.values():
            original = SOURCE / filename
            path = Path(folder) / filename
            base, count = probe_font(original, path)
            for grayscale in (False, True):
                face = freetype.Face(str(path))
                mode = '--prep-cvt-gray' if grayscale else '--prep-cvt'
                target = freetype.FT_LOAD_TARGET_NORMAL if grayscale else freetype.FT_LOAD_TARGET_MONO
                for size in SIZES:
                    result = subprocess.run(
                        [sys.argv[1], mode, str(original), str(size)],
                        capture_output=True, text=True, timeout=30,
                    )
                    assert not result.returncode, (filename, size, result.stdout, result.stderr)
                    native = list(map(int, result.stdout.split()))[8:]
                    assert len(native) == count, (filename, size, len(native), count)
                    face.set_pixel_sizes(size, size)
                    for i, expected in enumerate(native):
                        face.load_glyph(
                            base + i, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT | target
                        )
                        actual = face.glyph.outline.points[0][0]
                        checked += 1
                        if actual != expected:
                            failures.append((filename, size, grayscale, i, actual, expected))
                del face
    assert not failures, failures[:25]
    print(f'TrueType preparation reference: {checked} CVT comparisons, 0 failures; '
          f'engine {freetype.version()}, legacy monochrome/grayscale configurations')


if __name__ == '__main__':
    main()
