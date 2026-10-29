#!/usr/bin/env python3
"""Optional comparisons of authored point programs on one scaled triangle."""
from array import array
from collections import Counter
import random
import subprocess
import sys
import tempfile
from pathlib import Path
from pdf_tt_reference import outline_state

try:
    import freetype
    from fontTools.fontBuilder import FontBuilder
    from fontTools.ttLib import newTable
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib.tables.ttProgram import Program
except ImportError:
    print('TrueType point reference: skipped (optional fontTools/freetype-py unavailable)')
    raise SystemExit(0)


def push(*values):
    data = bytearray([0xb8 + len(values) - 1])
    for value in values:
        data.extend((value & 65535).to_bytes(2, 'big'))
    return data


def cases():
    programs = []
    for point in range(3):
        for axis in (0, 1):
            for mode in (0x18, 0x19, 0x3d, 0x7a, 0x7c, 0x7d):
                for value in (-257, -97, -65, -32, -1, 0, 1, 31, 32, 63, 64, 97, 257):
                    code = bytes([axis, mode]) + push(point, value) + bytes([0x48])
                    programs.append(code + push(point) + bytes([0x2f]))
    for op in (0x06, 0x07, 0x08, 0x09, 0x86, 0x87):
        for a in range(3):
            for b in range(3):
                code = push(a, b) + bytes([op])
                # Read the selected vector into glyph coordinates after resetting
                # both axes, so the probe movement cannot distort that value.
                getter = 0x0d if op in (0x08, 0x09) else 0x0c
                for component in (0, 1):
                    extract = bytes([0x23, 0x21]) if component else bytes([0x21])
                    programs.append(push(0) + code + bytes([getter]) + extract + bytes([0x01, 0x48]))
    for op in (0x06, 0x07, 0x86, 0x87):
        for a, b in ((0, 1), (1, 0), (1, 2), (2, 1)):
            # Exercise movement with diagonal projection and freedom vectors.
            code = push(1, 210) + bytes([0x48]) + push(a, b) + bytes([op, 0x0e])
            programs.append(code + push(2, 73) + bytes([0x48]))
            # Original coordinates are measured through the dual vector.
            programs.append(code + push(0, 2) + bytes([0x47, 0x01, 0x48]))
    for axis in (0, 1):
        for value in (-257, -1, 0, 1, 97, 257):
            for getter in (0x46, 0x47):
                code = bytes([axis]) + push(0) + bytes([0x16])
                code += push(1, value) + bytes([0x48])
                code += push(0, 1) + bytes([getter]) + push(1) + bytes([0x15, 0x48])
                programs.append(code)
    rng = random.Random(571)
    vectors = [(0, 0), (1, 1), (-32768, 32767), (3, 5)]
    vectors += [(rng.randrange(-32768, 32768), rng.randrange(-32768, 32768)) for _ in range(500)]
    for x, y in vectors:
        for setter, getter in ((0x0a, 0x0c), (0x0b, 0x0d)):
            for component in (0, 1):
                extract = bytes([0x23, 0x21]) if component else bytes([0x21])
                programs.append(push(0) + push(x, y) + bytes([setter, getter]) +
                                extract + bytes([0x01, 0x48]))
    # Coincident original twilight points with distinct current positions.
    coincident = push(0) + bytes([0x16]) + push(0, 31) + bytes([0x48])
    coincident += push(1, 31) + bytes([0x48]) + push(0) + bytes([0x2f])
    coincident += push(0, 1) + bytes([0x87])
    for component in (0, 1):
        extract = bytes([0x23, 0x21]) if component else bytes([0x21])
        programs.append(coincident + push(0) + bytes([0x0c]) + extract +
                        bytes([0x01]) + push(1) + bytes([0x15, 0x48]))
    programs.append(coincident + push(0, 0) + bytes([0x47, 0x01]) +
                    push(1) + bytes([0x15, 0x48]))
    # Coincident current points must not erase the distinct original direction.
    coincident = push(1, 31) + bytes([0x48, 0x00]) + push(1, 35) + bytes([0x48, 0x01])
    coincident += push(0, 1) + bytes([0x87])
    for component in (0, 1):
        extract = bytes([0x23, 0x21]) if component else bytes([0x21])
        programs.append(coincident + push(0) + bytes([0x0c]) + extract + bytes([0x01, 0x48]))
    programs.append(coincident + push(0, 2) + bytes([0x47, 0x01, 0x48]))
    # MIAP cut-in boundaries, both coordinate axes and all predefined rounds.
    for axis in (0, 1):
        for rounding in (0x18, 0x19, 0x3d, 0x7a, 0x7c, 0x7d):
            for target in (-257, -38, -37, -1, 0, 31, 32, 63, 64, 99, 100, 257):
                for cutin in (-1, 0, 16, 68, 257):
                    for op in (0x3e, 0x3f):
                        code = bytes([axis, rounding]) + push(0, target) + bytes([0x44])
                        code += push(cutin) + bytes([0x1d]) + push(0, 0) + bytes([op])
                        programs.append(code)
    for op in (0x3e, 0x3f):
        for target in (-257, -37, 0, 31, 32, 97, 257):
            for getter in (0x46, 0x47):
                code = push(0, target) + bytes([0x44]) + push(0) + bytes([0x16])
                code += push(3, 5) + bytes([0x0a, 0x0e]) + push(1, 0) + bytes([op])
                code += push(0, 1) + bytes([getter, 0x01]) + push(1) + bytes([0x15, 0x48])
                programs.append(code)
    for band, op in enumerate((0x5d, 0x71, 0x72)):
        for shift in range(7):
            for argument in range(256):
                base = 64 - band * 16 - (argument >> 4)
                for matching in (True, False):
                    code = push(base if matching else base + 1) + bytes([0x5e])
                    code += push(shift) + bytes([0x5f]) + push(argument, 1, 1) + bytes([op])
                    programs.append(code)
        # DELTA movement uses projection, unlike SHPIX's direct freedom movement.
        code = push(64 - band * 16) + bytes([0x5e]) + push(3, 5) + bytes([0x0b])
        programs.append(code + push(8, 0, 1) + bytes([op]))
        for getter in (0x46, 0x47):
            code = push(64 - band * 16) + bytes([0x5e]) + push(0) + bytes([0x16])
            code += push(8, 1, 1) + bytes([op]) + push(0, 1) + bytes([getter])
            code += push(1) + bytes([0x15, 0x48])
            programs.append(code)
    for freedom in ((16384, 0), (0, 16384), (9830, 13107), (-9830, 13107)):
        for distance in (-257, -97, -64, -1, 0, 1, 31, 64, 97, 257):
            for count in (0, 1, 3):
                code = push(*freedom) + bytes([0x0b]) + push(count) + bytes([0x17])
                code += push(*tuple(range(count)), distance) + bytes([0x38])
                programs.append(code)
    for op in (0x32, 0x33):
        setter = 0x11 if op & 1 else 0x12
        for axis in (0, 1):
            for distance in (-257, -65, -1, 0, 1, 31, 64, 257):
                for indices in ((0,), (1,), (2,), (0, 1, 2), (1, 1, 0)):
                    code = bytes([axis]) + push(1, distance) + bytes([0x48])
                    code += push(1) + bytes([setter]) + push(len(indices)) + bytes([0x17])
                    code += push(*indices) + bytes([op])
                    programs.append(code)
        code = push(0) + bytes([0x16]) + push(1, 31) + bytes([0x48])
        code += push(1) + bytes([0x2f]) + push(1) + bytes([setter])
        code += push(1) + bytes([0x15]) + push(1) + bytes([0x14 if op & 1 else 0x13])
        code += push(0) + bytes([op])
        programs.append(code)
        programs.append(push(0) + bytes([0x17, op]))
        for projection in ((16384, 0), (3, 4)):
            for freedom in ((3, 5), (-3, 5)):
                code = push(0, 97) + bytes([0x48]) + push(0) + bytes([setter])
                code += push(*projection) + bytes([0x0a]) + push(*freedom) + bytes([0x0b])
                programs.append(code + push(2) + bytes([op]))
    for op in range(0xe0, 0x100):
        if op & 3 == 3:
            continue
        for axis in (0, 1):
            for value in (-129, -65, 0, 31, 64, 97, 145, 257):
                for cutin in (0, 68, 76, 256):
                    for flip in (0x4d, 0x4e):
                        code = bytes([axis, flip]) + push(0, value) + bytes([0x44])
                        code += push(cutin) + bytes([0x1d]) + push(1, 0) + bytes([op])
                        programs.append(code)
        for minimum in (0, 16, 128):
            for value in (-97, 0, 31, 97):
                code = push(minimum) + bytes([0x1a]) + push(0, value) + bytes([0x44])
                programs.append(code + push(1, 0) + bytes([op]))
        programs.append(push(1, -1) + bytes([op]))
        # Observe all three reference-point assignments through later operations.
        for follow in (0x32, 0x33, 0x3c):
            code = bytes([0x00]) + push(2, 123) + bytes([0x48])
            code += push(2) + bytes([0x10]) + push(1, 0) + bytes([op])
            programs.append(code + push(0) + bytes([follow]))
    for op in (0xe0, 0xe4, 0xec, 0xf4):
        for width in (0, 32, 64, 128):
            for cutin in (0, 1, 16, 64):
                values = (width - cutin - 1, width - cutin, width - cutin + 1,
                          width, width + cutin - 1, width + cutin, width + cutin + 1, -width - 1)
                for value in sorted(set(values)):
                    code = push(width) + bytes([0x1f]) + push(cutin) + bytes([0x1e])
                    code += push(0, value) + bytes([0x44]) + push(1, 0) + bytes([op])
                    programs.append(code)
    for op in (0x3a, 0x3b):
        for axis in (0, 1):
            for value in (-257, -97, -1, 0, 1, 31, 64, 97, 257):
                code = bytes([axis, 0x19]) + push(1, value) + bytes([op])
                programs.append(code)
                for getter in (0x46, 0x47):
                    code = push(0, 97) + bytes([0x48]) + push(0) + bytes([0x14])
                    code += push(1, value) + bytes([op]) + push(2, 1) + push(0) + bytes([0x15, getter])
                    code += push(1) + bytes([0x15, 0x48])
                    programs.append(code)
    for axis in (0, 1):
        for reference in range(3):
            for indices in ((0,), (1,), (2,), (0, 1, 2), (2, 2, 0)):
                code = bytes([axis]) + push(reference) + bytes([0x10])
                code += push(len(indices)) + bytes([0x17]) + push(*indices) + bytes([0x3c])
                programs.append(code)
    for op in (0xe0, 0xe4, 0xec, 0xf4, 0xfc):
        for value in (-129, -31, 0, 31, 97, 145):
            for context in range(4):
                for getter in (0x46, 0x47):
                    code = push(0, value) + bytes([0x44])
                    if context == 0:
                        # Glyph reference, twilight destination.
                        code += push(0, 97) + bytes([0x48]) + push(0) + bytes([0x14])
                        target = 1
                    else:
                        code += push(0) + bytes([0x16]) + push(0, 31) + bytes([0x48])
                        code += push(0) + bytes([0x2f])
                        target = 0 if context == 3 else 1
                        if context == 1:
                            # Twilight reference, glyph destination.
                            code += push(1) + bytes([0x14])
                    code += push(target, 0) + bytes([op])
                    source_zone = 1 if context == 1 else 0
                    code += push(2, target) + push(source_zone) + bytes([0x15, getter, 0x01])
                    code += push(1) + bytes([0x15, 0x48])
                    programs.append(code)
        # Distinct current/original vector directions and disabled cut-in.
        code = push(1, -100) + bytes([0x48]) + push(0, 1) + bytes([0x86, 0x0e])
        code += push(256) + bytes([0x1d]) + push(2, 0) + bytes([op])
        programs.append(code)
    for op in (0x3a, 0x3b):
        for distance in (-129, -31, 0, 31, 97):
            for getter in (0x46, 0x47):
                code = push(0, 97) + bytes([0x48]) + push(0) + bytes([0x14])
                code += push(3, 5) + bytes([0x0b]) + push(1, distance) + bytes([op])
                code += push(2, 1) + push(0) + bytes([0x15, getter, 0x01])
                code += push(1) + bytes([0x15, 0x48])
                programs.append(code)
    for op in (0xe0, 0xe4, 0xec, 0xf4):
        for value in (-145, -31, 31, 145):
            for getter in (0x46, 0x47):
                code = push(0, value) + bytes([0x44]) + push(0) + bytes([0x16])
                code += push(0, 31) + bytes([0x48]) + push(0) + bytes([0x2f])
                code += push(3, 5) + bytes([0x0b]) + push(1, 0) + bytes([op])
                code += push(2, 1) + bytes([getter, 0x01]) + push(1) + bytes([0x15, 0x48])
                programs.append(code)
    for freedom in ((16384, 0), (3, 5), (-3, 5)):
        code = push(1, 97) + bytes([0x48]) + push(1) + bytes([0x10])
        code += push(*freedom) + bytes([0x0b]) + push(3) + bytes([0x17])
        programs.append(code + push(0, 1, 2) + bytes([0x3c]))
    for op in range(0xc0, 0xe0):
        if op & 3 == 3:
            continue
        for target in range(3):
            for reference in range(3):
                for axis in (0, 1):
                    for mode in (0x18, 0x19, 0x7a, 0x7c, 0x7d):
                        code = bytes([axis, mode]) + push(reference, 97) + bytes([0x48])
                        code += push(reference) + bytes([0x10])
                        programs.append(code + push(target) + bytes([op]))
    for width in (-145, -69, -31, 0, 31, 69, 145):
        for cutin in (0, 1, 38, 69, 70, 200):
            for target, reference in ((0, 1), (1, 0)):
                for op in (0xc0, 0xc4, 0xc8, 0xcc):
                    code = push(width) + bytes([0x1f]) + push(cutin) + bytes([0x1e])
                    code += push(reference) + bytes([0x10]) + push(target) + bytes([op])
                    programs.append(code)
    for axis in (0, 1):
        for a in range(3):
            for b in range(3):
                if a == b:
                    continue
                for value_a in (-145, -1, 31, 97, 257):
                    for value_b in (-145, -1, 31, 97, 257):
                        code = bytes([axis]) + push(a, value_a) + bytes([0x48])
                        code += push(b, value_b) + bytes([0x48])
                        code += push(a) + bytes([0x11]) + push(b) + bytes([0x12])
                        for target in range(3):
                            programs.append(code + push(target) + bytes([0x39]))
                        programs.append(code + push(4) + bytes([0x17]) + push(0, 1, 2, 0) + bytes([0x39]))
        for mask in range(8):
            for value in (-145, -1, 31, 97, 257):
                code = bytes([axis])
                for point in range(3):
                    if mask & (1 << point):
                        code += push(point, value + point * 29) + bytes([0x48])
                programs.append(code + bytes([0x30 + axis]))
                if bin(mask).count('1') >= 2:
                    programs.append(code + bytes([0x30 + axis, 0x30 + axis]))
    for op in (0x49, 0x4a):
        for a in range(3):
            for b in range(3):
                for axis in (0, 1):
                    for value in (-97, 31, 145):
                        code = bytes([axis]) + push(a, value) + bytes([0x48])
                        # Leave an output point below the measured pair.
                        code += push(2, a, b) + bytes([op, 0x01, 0x48])
                        programs.append(code)
    for a, b in ((0, 1), (1, 0), (0, 2), (2, 0), (1, 2), (2, 1)):
        for value in (-97, 31, 145):
            base = push(a, value) + bytes([0x48]) + push(a, b) + bytes([0x86, 0x0e])
            for target in range(3):
                for op in (0xc0, 0xc4, 0xcc, 0xdc):
                    programs.append(base + push(a) + bytes([0x10]) + push(target) + bytes([op]))
                programs.append(base + push(a) + bytes([0x11]) + push(b) + bytes([0x12]) +
                                push(target) + bytes([0x39]))
            for op in (0x49, 0x4a):
                programs.append(base + push(2, a, b) + bytes([op, 0x01, 0x48]))
    for op in (0x34, 0x35):
        for reference in range(3):
            for axis in (0, 1):
                for value in (-97, -31, 31, 97, 145):
                    for freedom in ((0, 16384) if not axis else (16384, 0), (3, 5), (-3, 5)):
                        code = bytes([axis]) + push(reference, value) + bytes([0x48])
                        code += push(reference) + bytes([0x11 if op & 1 else 0x12])
                        code += push(*freedom) + bytes([0x0b]) + push(0) + bytes([op])
                        programs.append(code)
    for op in (0x34, 0x35):
        for axis in (0, 1):
            for value in (-97, 31, 97, 145):
                for twilight_reference in (False, True):
                    code = bytes([axis])
                    if twilight_reference:
                        code += push(0) + bytes([0x16]) + push(0, value) + bytes([0x44])
                        code += push(0, 0) + bytes([0x3f])
                    else:
                        code += push(0, value) + bytes([0x48]) + push(0) + bytes([0x15])
                    code += push(0) + bytes([0x11 if op & 1 else 0x12]) + push(0) + bytes([op])
                    code += push(2, 1) + bytes([0x46, 0x01]) + push(1) + bytes([0x15, 0x48])
                    programs.append(code)
    for point in range(3):
        flip = push(point) + bytes([0x80])
        programs.extend((flip, flip + flip))
    for sequence in ((), (0,), (0, 0), (0, 1), (0, 1, 2), (2, 1, 2, 0)):
        code = push(len(sequence)) + bytes([0x17])
        if sequence:
            code += push(*sequence)
        programs.append(code + bytes([0x80]))
    for op in (0x81, 0x82):
        for low in range(3):
            for high in range(3):
                prefix = push(0, 2) + bytes([0x82]) if op == 0x81 else b''
                code = prefix + push(3) + bytes([0x17]) + push(low, high) + bytes([op])
                programs.extend((code, code + push(0, 1, 2) + bytes([0x80])))
    for op in (0x36, 0x37):
        for axis in (0, 1):
            for value in (-145, -31, 31, 97, 145):
                for freedom in ((0, 16384) if not axis else (16384, 0), (3, 5), (-3, 5)):
                    code = bytes([axis]) + push(0, value) + bytes([0x48])
                    code += push(*freedom) + bytes([0x0b]) + push(1) + bytes([op])
                    programs.append(code)
                for twilight_reference in (False, True):
                    code = bytes([axis])
                    if twilight_reference:
                        code += push(0) + bytes([0x16]) + push(0, value) + bytes([0x44])
                        code += push(0, 0) + bytes([0x3f]) + push(1) + bytes([0x15])
                        code += push(1) + bytes([op])
                    else:
                        code += push(0, value) + bytes([0x48]) + push(0) + bytes([0x15])
                        code += push(0) + bytes([op])
                        code += push(2, 1) + bytes([0x46, 0x01]) + push(1) + bytes([0x15, 0x48])
                    programs.append(code)
    for axis in (0, 1):
        for a in range(3):
            for b in range(3):
                programs.append(bytes([axis]) + push(a, b) + bytes([0x27]))
        for za in (0, 1):
            for zb in (0, 1):
                for a in range(2):
                    for b in range(2):
                        code = bytes([axis]) + push(za) + bytes([0x14]) + push(zb) + bytes([0x13])
                        code += push(a, b) + bytes([0x27])
                        # Copy the twilight result into a visible glyph point.
                        code += push(2) + push(0) + bytes([0x15]) + push(a) + bytes([0x46])
                        code += push(1) + bytes([0x15, 0x48])
                        programs.append(code)
    # The reference face can retain twilight storage between glyph loads.
    # Give both engines the same explicit initial coordinates for each program.
    reset = push(0) + bytes([0x16, 0x01])
    reset += push(0, 0) + bytes([0x48]) + push(1, 0) + bytes([0x48, 0x00])
    reset += push(0, 0) + bytes([0x48]) + push(1, 0) + bytes([0x48])
    reset += push(1) + bytes([0x16, 0x01])
    return [reset + code for code in programs]


def build_font(programs, path):
    order = ['.notdef'] + [f'g{i}' for i in range(len(programs))]
    glyphs = {}
    for i, name in enumerate(order):
        pen = TTGlyphPen(None)
        pen.moveTo((31, 35))
        pen.lineTo((100, 0))
        pen.lineTo((0, 100))
        pen.closePath()
        glyph = pen.glyph()
        if i:
            glyph.program = Program()
            glyph.program.fromBytecode(programs[i - 1])
        glyphs[name] = glyph
    # At 64 ppem, 4096 units per em gives an exact design-unit-to-F26Dot6 mapping.
    font = FontBuilder(4096, isTTF=True)
    font.setupGlyphOrder(order)
    font.setupCharacterMap({65: 'g0'})
    font.setupGlyf(glyphs)
    font.setupHorizontalMetrics({name: (128, 0) for name in order})
    font.setupHorizontalHeader(ascent=128, descent=0)
    font.setupNameTable({'familyName': 'VM Point Test', 'styleName': 'Regular',
                        'uniqueFontIdentifier': 'VM Point Test', 'fullName': 'VM Point Test',
                        'psName': 'VMPointTest'})
    font.setupOS2(sTypoAscender=128, sTypoDescender=0, usWinAscent=128, usWinDescent=0)
    font.setupPost()
    font.setupMaxp()
    font.font['maxp'].maxStackElements = 64
    font.font['maxp'].maxTwilightPoints = 2
    font.font['cvt '] = newTable('cvt ')
    font.font['cvt '].values = array('h', [64, 128, -64, 0])
    font.save(path)


def special_states(kind, ppem):
    scaled = lambda value: (value * ppem + 32) // 64
    base = [(scaled(31), scaled(35)), (scaled(100), 0), (0, scaled(100))]
    expected_on = actual_on = (1, 1, 1)
    expected = actual = base
    if kind in ('iup', 'shift-zone'):
        shift = 97 - scaled(31)
        expected = [(97, scaled(35)), (scaled(100) + shift, 0), (shift, scaled(100))]
        factor = 2 if kind == 'iup' else 0
        actual = [(97, scaled(35)), (scaled(100) + factor * shift, 0), (factor * shift, scaled(100))]
    elif kind == 'flip-zone':
        actual_on = (0, 1, 1)
    elif kind == 'off-zone':
        actual_on = (0, 0, 1)
    elif kind == 'on-zone':
        expected_on = (0, 0, 1)
    def flatten(points, on):
        return [value for i, point in enumerate(points) for value in (*point, on[i], int(i == 2))]
    return flatten(expected, expected_on), flatten(actual, actual_on)


def main():
    programs = cases()
    specials = {}
    # Keep specified behavior separate where tested engines disagree.
    for kind, code in (
        ('iup', push(0, 97) + bytes([0x48, 0x31, 0x31])),
        ('flip-zone', push(0) + bytes([0x13]) + push(0) + bytes([0x80])),
        ('off-zone', push(0) + bytes([0x13]) + push(0, 1) + bytes([0x82])),
        ('on-zone', push(0, 1) + bytes([0x82]) + push(0) + bytes([0x13]) + push(0, 1) + bytes([0x81])),
        ('shift-zone', push(0, 97) + bytes([0x48]) + push(0) + bytes([0x15]) + push(1) + bytes([0x36])),
        ('shift-zone', push(0, 97) + bytes([0x48]) + push(0) + bytes([0x15]) + push(1) + bytes([0x37])),
    ):
        specials[len(programs)] = kind
        programs.append(code)
    failures = []
    differences = Counter()
    sizes = (13, 37, 64, 97)
    with tempfile.TemporaryDirectory(prefix='cupid-tt-points-') as directory:
        for ppem in sizes:
            result = subprocess.run([sys.argv[1], '--point-state', str(ppem)],
                                    input=''.join(code.hex() + '\n' for code in programs),
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, (ppem, result.returncode, result.stderr)
            native = [list(map(int, line.split())) for line in result.stdout.splitlines()]
            assert len(native) == len(programs)
            for start in range(0, len(programs), 20000):
                batch = programs[start:start + 20000]
                path = Path(directory) / f'points-{start}.ttf'
                if not path.exists():
                    build_font(batch, path)
                face = freetype.Face(str(path))
                face.set_pixel_sizes(ppem, ppem)
                for i, code in enumerate(batch):
                    face.load_glyph(i + 1, freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_AUTOHINT |
                                    freetype.FT_LOAD_TARGET_MONO)
                    actual = [value for point in outline_state(face) for value in point]
                    kind = specials.get(start + i)
                    if kind:
                        expected, prior_behavior = special_states(kind, ppem)
                        assert native[start + i] == expected, (kind, ppem, native[start + i], expected)
                        if actual == prior_behavior and actual != expected:
                            differences[kind] += 1
                            continue
                    if native[start + i] != actual:
                        failures.append((ppem, start + i, code.hex(), native[start + i], actual))
                del face
    assert not failures, failures[:20]
    print(f'TrueType point reference: {len(programs)} programs at {len(sizes)} sizes '
          f'({len(programs) * len(sizes) - sum(differences.values())} matches, '
          f'specified differences {dict(differences)}), 0 unexpected failures; '
          f'engine {freetype.version()}; coordinates/curve flags/contours; page renderer not involved')


if __name__ == '__main__':
    main()
