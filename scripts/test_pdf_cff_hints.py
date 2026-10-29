#!/usr/bin/env python3
"""Check retained private hint parameters without claiming grid fitting."""
import io
from pathlib import Path
import subprocess
import sys
import tempfile

from test_pdf_cff import fixture, integer, real
from generate_pdf_standard_outlines import FONTS, ROOT, font_bytes

FIELDS = ('BlueValues', 'OtherBlues', 'FamilyBlues', 'FamilyOtherBlues',
          'StemSnapH', 'StemSnapV', 'BlueScale', 'BlueShift', 'BlueFuzz',
          'StdHW', 'StdVW', 'ForceBold', 'LanguageGroup', 'ExpansionFactor')
DEFAULTS = ([], [], [], [], [], [], .039625, 7, 1, 0, 0, 0, 0, .06)
OPS = (6, 7, 8, 9, 268, 269, 265, 266, 267, 10, 11, 270, 273, 274)


def entry(field, values):
    op = OPS[field]
    return b''.join(integer(v) if isinstance(v, int) else real(v) for v in values) + (
        bytes([op]) if op < 256 else bytes([12, op - 256]))


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='cupid-cff-hints-') as tmp:
        path = Path(tmp) / 'font.cff'

        def read(data):
            path.write_bytes(data)
            result = subprocess.run([probe, str(path), '--hints'], capture_output=True,
                                    text=True, check=True, timeout=10)
            values, masks = {}, {}
            for line in result.stdout.splitlines():
                tokens = line.split()
                if tokens[0] == 'hints':
                    masks[int(tokens[1])] = tuple(map(int, tokens[2:]))
                elif tokens[0] in ('hint-array', 'hint-scalar'):
                    fd, field = map(int, tokens[1:3])
                    if tokens[0] == 'hint-array':
                        values[fd, field] = list(map(float, tokens[3:]))
                    else:
                        values[fd, field + 6] = float(tokens[3])
            return values, masks

        encoded = [[-20, 20, 700, 12], [-200, 10], [-22, 22, 700, 14],
                   [-202, 12], [50, 20, 10], [60, 30], [.04], [8], [2],
                   [70], [90], [1], [1], [.07]]
        prefix = b''.join(entry(i, v) for i, v in enumerate(encoded))
        data = fixture(cid=True, private_count=2, private_prefixes=[prefix, b''])
        values, masks = read(data)
        assert masks == {0: ((1 << 14) - 1, 0), 1: (0, 0)}
        for i, raw in enumerate(encoded):
            expected = list(raw) if i < 6 else raw[0]
            if i < 6:
                for j in range(1, len(expected)):
                    expected[j] += expected[j - 1]
            assert values[0, i] == expected, (i, values[0, i], expected)
            assert values[1, i] == DEFAULTS[i]
            checks += 2
        # Wrong arity, odd blue lists, invalid booleans and order retain diagnostics.
        for field, raw in ((0, [1]), (4, list(range(13))), (6, []), (11, [2]),
                           (11, [1.0]), (12, [2]), (1, [-200, 10])):
            values, masks = read(fixture(private_prefixes=[entry(field, raw)]))
            assert masks[0] == (1 << field, 1 << field)
            assert values[0, field] == DEFAULTS[field]
            checks += 1
        # A later valid value replaces an invalid value and its diagnostic.
        values, masks = read(fixture(private_prefixes=[entry(0, [1]) + entry(0, [-20, 20])]))
        assert masks[0] == (1, 0) and values[0, 0] == [-20, 0]
        checks += 1
        print(f'CFF private hint parameters: {checks} authored checks passed')
        try:
            from fontTools.cffLib import CFFFontSet
        except ImportError:
            print('CFF private hint reference checks skipped: FontTools unavailable')
            return
        fonts = [(name, font_bytes(stem)) for name, stem in FONTS.items()]
        fonts += [(p.name, p.read_bytes()) for p in sorted(
            (ROOT / 'third_party/standard-spacing-marks').glob('*.cff'))]
        comparisons = 0
        for name, data in fonts:
            cff = CFFFontSet()
            cff.decompile(io.BytesIO(data), None)
            private = cff.topDictIndex[0].Private
            values, masks = read(data)
            assert masks[0][1] == 0, (name, masks)
            for field, key in enumerate(FIELDS):
                expected = getattr(private, key, DEFAULTS[field])
                assert values[0, field] == expected, (name, key, values[0, field], expected)
                comparisons += 1
        print(f'CFF private hint parameters: {comparisons} independent field comparisons passed')


if __name__ == '__main__':
    main()
