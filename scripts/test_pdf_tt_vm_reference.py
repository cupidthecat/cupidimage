#!/usr/bin/env python3
"""Optional instruction-level comparisons through synthetic TrueType outlines.

The native page renderer is not involved. fontTools and freetype-py are test-only
optional dependencies; the generated fonts contain only authored numeric programs.
"""
import sys
import math
import subprocess
import tempfile
from pathlib import Path
try:
    import freetype
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib.tables.ttProgram import Program
except ImportError:
    print('TrueType numeric reference: skipped (optional fontTools/freetype-py unavailable)')
    raise SystemExit(0)

def push(values):
    out = bytearray([0xb8 + len(values) - 1])
    for v in values:
        out.extend((v & 65535).to_bytes(2, 'big'))
    return out
cases = []
for op in (0x18, 0x19, 0x3d, 0x7a, 0x7c, 0x7d):
    for value in (-1025, -129, -128, -97, -96, -65, -64, -33, -32, -31, -1, 0, 1, 31, 32, 33, 63, 64, 65, 95, 96, 97, 127, 128, 129, 1025):
        a = abs(value)
        rounded = {
            0x18: math.floor(a / 64 + 0.5) * 64,
            0x19: math.floor(a / 64) * 64 + 32,
            0x3d: math.floor(a / 32 + 0.5) * 32,
            0x7a: a,
            0x7c: math.ceil(a / 64) * 64,
            0x7d: math.floor(a / 64) * 64,
        }[op]
        cases.append((bytes([op]) + push([value]) + bytes([0x68]), -rounded if value < 0 else rounded, f'{op:02x}:{value}'))
for diagonal in (False, True):
    for selector in range(192):
        fine = (11585 if diagonal else 16384) * (1 << (selector >> 6)) // 2
        p = fine // 256
        phase = ((selector >> 4 & 3) * fine // 4) // 256
        t = selector & 15
        t = math.trunc((t - 4) * fine / 8) // 256 if t else (fine - 1) // 256
        for value in (-513, -128, -65, -32, -1, 0, 1, 15, 16, 31, 32, 63, 64, 65, 127, 128, 513):
            answer = max(phase, (abs(value) - phase + t) // p * p + phase)
            opcode = 0x77 if diagonal else 0x76
            cases.append((push([selector]) + bytes([opcode]) + push([value]) + bytes([0x68]),
                          -answer if value < 0 else answer, f'S{opcode:02x}:{selector}:{value}'))
for a in (-32768, -32767, -257, -65, -33, -32, -31, -1, 0, 1, 31, 32, 33, 63, 64, 65, 257, 32767):
    for b in (-257, -100, -65, -64, -33, -32, -31, -3, -1, 1, 3, 31, 32, 33, 64, 65, 100, 257):
        for op in (0x62, 0x63):
            if op == 0x62:
                answer = math.trunc(a * 64 / b)
            else:
                answer = (abs(a * b) + 32) // 64 * (-1 if a * b < 0 else 1)
            cases.append((push([a, b]) + bytes([op]), answer, f'{op:02x}:{a}:{b}'))
order = ['.notdef'] + [f'g{i}' for i in range(len(cases))]
glyphs = {}
for i, name in enumerate(order):
    pen = TTGlyphPen(None)
    pen.moveTo((0, 0))
    pen.lineTo((1000, 0))
    pen.lineTo((0, 1000))
    pen.closePath()
    g = pen.glyph()
    if i:
        # SVTCA[x], point 0, expression, SCFS exposes the numeric result as
        # the first outline point's x coordinate in the independent interpreter.
        g.program = Program()
        g.program.fromBytecode(bytes([0x01]) + push([0]) + cases[i - 1][0] + bytes([0x48]))
    glyphs[name] = g
fb = FontBuilder(1000, isTTF=True)
fb.setupGlyphOrder(order)
fb.setupCharacterMap({65: 'g0'})
fb.setupGlyf(glyphs)
fb.setupHorizontalMetrics({name: (1000, 0) for name in order})
fb.setupHorizontalHeader(ascent=1000, descent=0)
fb.setupNameTable({'familyName': 'VM Numeric Test', 'styleName': 'Regular', 'uniqueFontIdentifier': 'VM Numeric Test', 'fullName': 'VM Numeric Test', 'psName': 'VMNumericTest'})
fb.setupOS2(sTypoAscender=1000, sTypoDescender=0, usWinAscent=1000, usWinDescent=0)
fb.setupPost()
fb.setupMaxp()
fb.font['maxp'].maxStackElements = 64
with tempfile.TemporaryDirectory(prefix='cupid-tt-numeric-') as directory:
    path = Path(directory) / 'numeric.ttf'
    fb.save(path)
    face = freetype.Face(str(path))
    # Monochrome outline loading avoids horizontal subpixel-hint suppression.
    face.set_pixel_sizes(16, 16)
    result = subprocess.run(
        [sys.argv[1], '--expressions'],
        input=''.join(code.hex() + '\n' for code, _, _ in cases),
        capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, (result.returncode, result.stderr)
    native = list(map(int, result.stdout.split()))
    assert len(native) == len(cases), (len(native), len(cases))
    failures = []
    for i, (_, expected, name) in enumerate(cases):
        face.load_glyph(i + 1, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT | freetype.FT_LOAD_TARGET_MONO)
        actual = face.glyph.outline.points[0][0]
        if actual != expected or native[i] != actual:
            failures.append((name, native[i], actual, expected))
    del face
assert not failures, failures[:30]
print(f'TrueType numeric reference: {len(cases)} comparisons, 0 failures; engine {freetype.version()}')
