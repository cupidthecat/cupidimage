#!/usr/bin/env python3
"""Check native phantom construction and metric fallbacks with authored fonts."""
import ctypes
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import freetype
    from fontTools.ttLib import TTFont, newTable
except ImportError:
    print('TrueType loader reference: skipped (optional fontTools/freetype-py unavailable)')
    raise SystemExit(0)

from test_pdf_tt_points_reference import build_font, push
from test_pdf_truetype import replace_tables, tables

from pdf_tt_reference import outline_state


def expected_outline(bearing, ppem, axis, phantom, original, top, bottom):
    def scaled(value):
        value = ((abs(value) * ppem + 32) // 64) * (-1 if value < 0 else 1)
        return value
    def rounded(value):
        return (value + 32) // 64 * 64
    horizontal = (-bearing, 512 - bearing, 0, 0)
    vertical = (0, 0, top, bottom)
    index = phantom - 3
    value = scaled((horizontal if axis else vertical)[index])
    if not original and (axis == 1) == (index < 2):
        value = rounded(value)
    origin = rounded(scaled(-bearing))
    return [(value - origin, scaled(35), 1, 0), (scaled(100) - origin, 0, 1, 0), (-origin, scaled(100), 1, 1)]


def main():
    version = ctypes.c_uint(35)
    assert freetype.FT_Property_Set(freetype.get_handle(), b'truetype', b'interpreter-version',
                                   ctypes.byref(version)) == 0
    checked = 0
    legacy_differences = 0
    with tempfile.TemporaryDirectory(prefix='cupid-tt-loader-') as directory:
        path = Path(directory) / 'metrics.ttf'
        for mode in ('os2', 'hhea', 'legacy-os2', 'vmtx'):
            for bearing in (-19, 0, 19):
                for ppem in (13, 37, 64):
                    for axis in (0, 1):
                        for phantom in range(3, 7):
                            for original in (0, 1):
                                code = bytes([axis]) + push(0, phantom) + bytes([0x46 + original,
                                                                              0x01, 0x48])
                                build_font([code], path)
                                with TTFont(path) as font:
                                    font['head'].flags &= ~2
                                    for name in font.getGlyphOrder():
                                        font['hmtx'].metrics[name] = (512, bearing)
                                    font['OS/2'].sTypoAscender = 311
                                    font['OS/2'].sTypoDescender = -97
                                    font['hhea'].ascent = 256
                                    font['hhea'].descent = -64
                                    if mode == 'hhea':
                                        del font['OS/2']
                                    if mode == 'vmtx':
                                        font['vhea'] = newTable('vhea')
                                        values = dict(tableVersion=0x00010000, ascent=333, descent=-101,
                                                      lineGap=0, advanceHeightMax=777,
                                                      minTopSideBearing=bearing,
                                                      minBottomSideBearing=0, yMaxExtent=100,
                                                      caretSlopeRise=0, caretSlopeRun=1, caretOffset=0,
                                                      reserved1=0, reserved2=0, reserved3=0, reserved4=0,
                                                      metricDataFormat=0, numberOfVMetrics=2)
                                        for key, value in values.items():
                                            setattr(font['vhea'], key, value)
                                        font['vmtx'] = newTable('vmtx')
                                        font['vmtx'].metrics = {name: (777, bearing)
                                                               for name in font.getGlyphOrder()}
                                    font.save(path)
                                if mode == 'legacy-os2':
                                    data = path.read_bytes()
                                    _, offset, _ = tables(data)[b'OS/2']
                                    shortened = b'\0\0' + data[offset + 2:offset + 68]
                                    path.write_bytes(replace_tables(data, {b'OS/2': shortened}))
                                result = subprocess.run([sys.argv[1], str(path), '--native',
                                                         str(ppem), '1'], capture_output=True,
                                                        text=True, timeout=20)
                                assert result.returncode == 0, result.stderr
                                lines = result.stdout.splitlines()
                                status = list(map(int, lines[0].split()))
                                assert status[:4] == [1, 0, 0, 0], (mode, bearing, ppem, code.hex(), status)
                                native = [tuple(map(int, line.split())) for line in lines[1:]]
                                face = freetype.Face(str(path))
                                face.set_pixel_sizes(ppem, ppem)
                                face.load_glyph(1, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT |
                                                freetype.FT_LOAD_TARGET_MONO)
                                actual = outline_state(face)
                                top, bottom = ((311, -97) if mode == 'os2' else
                                               (100 + bearing, 100 + bearing - 777) if mode == 'vmtx' else
                                               (256, -64))
                                expected = expected_outline(bearing, ppem, axis, phantom, original, top, bottom)
                                assert native == expected, (mode, bearing, ppem, code.hex(), native, expected)
                                if native != actual and mode == 'legacy-os2':
                                    # Older comparison engines read typographic fields beyond the
                                    # shortened table into the following table's bytes. Never copy
                                    # that behavior into the native loader or count it as a match.
                                    data = path.read_bytes()
                                    _, offset, length = tables(data)[b'OS/2']
                                    assert length == 68
                                    adjacent_ascent, adjacent_descent = struct.unpack_from('>hh', data, offset + length)
                                    observed = expected_outline(bearing, ppem, axis, phantom, original,
                                                                adjacent_ascent,
                                                                adjacent_ascent - abs(adjacent_ascent - adjacent_descent))
                                    assert actual == observed, (mode, ppem, actual, observed)
                                    legacy_differences += 1
                                else:
                                    assert native == actual, (mode, bearing, ppem, code.hex(), native, actual)
                                checked += 1
                                del face
    print(f'TrueType loader reference: {checked} analytic cases, '
          f'{checked - legacy_differences} independent matches, {legacy_differences} shortened-table differences; '
          f'engine {freetype.version()}; native simple-glyph loading')


if __name__ == '__main__':
    main()
