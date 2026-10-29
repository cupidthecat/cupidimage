#!/usr/bin/env python3
"""Check literal Type 1 private hints and explicit unresolved-expression diagnostics."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_type1 import fixture
from test_pdf_cff_hints import FIELDS, DEFAULTS


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='cupid-type1-hints-') as tmp:
        path = Path(tmp) / 'font.bin'

        def read(extra, **kwargs):
            nonlocal checks
            data, clear, encrypted = fixture(extra=extra.encode(), **kwargs)
            path.write_bytes(data)
            out = subprocess.run([probe, str(path), str(clear), str(encrypted), '--hints'],
                                 capture_output=True, text=True, check=True, timeout=10)
            values = {}
            masks = None
            for line in out.stdout.splitlines():
                parts = line.split()
                if parts[0] == 'hints':
                    masks = tuple(map(int, parts[1:]))
                elif parts[0] == 'hint-array':
                    values[int(parts[1])] = list(map(float, parts[2:]))
                elif parts[0] == 'hint-scalar':
                    values[int(parts[1])] = float(parts[2])
            checks += 1
            return masks, values

        raw = ([-20, 0, 700, 712], [-200, -190], [-22, 0, 700, 714], [-202, -190],
               [50, 70, 80], [60, 90], .04, 8, 2, [70], [90], 'true', 1, .07)
        expected = list(raw)
        expected[9:12] = [70, 90, 1]
        entries = []
        for name, value in zip(FIELDS, raw):
            text = ('[' + ' '.join(map(str, value)) + ']') if isinstance(value, list) else str(value)
            entries.append(f'/{name} {text} def\n')
        for outer in (True, False):
            masks, values = read(''.join(entries), outer_encrypted=outer)
            assert masks == ((1 << 14) - 1, 0, 0), masks
            assert [values[i] for i in range(14)] == expected, values
        for end in ('def', 'readonly def', 'noaccess def', 'ND'):
            masks, values = read('/BlueValues [-20 0] ' + end)
            assert masks == (1, 0, 0) and values[0] == [-20, 0], (end, masks, values)
        masks, values = read('/BlueValues {-20 0} def\n/StdHW {70} def')
        assert masks == (513, 0, 0) and values[0] == [-20, 0] and values[9] == 70
        invalid = ((0, '[1]'), (0, '[0 -20]'), (0, '[-1.5 0]'), (4, '[' + ' '.join(map(str, range(13))) + ']'),
                   (6, '-1'), (6, 'true'), (0, 'false'), (7, '1.25'), (9, '[1 2]'), (9, '70'), (11, '1'), (12, '2'))
        for field, expression in invalid:
            masks, values = read(f'/{FIELDS[field]} {expression} def')
            assert masks == (1 << field, 1 << field, 0), (field, masks)
            assert values[field] == DEFAULTS[field], (field, values)
        unresolved = ((6, 'ComputedScale'), (0, '//SavedZones'), (11, 'BoldPolicy'), (0, '[-20 0] dup'), (0, '[1 2 div 0]'), (6, '1 25 div'),
                      (4, '[50 70] true {pop [60]} if'))
        for field, expression in unresolved:
            masks, values = read(f'/{FIELDS[field]} {expression} def')
            assert masks == (1 << field,) * 3, (field, masks)
            assert values[field] == DEFAULTS[field]
        masks, values = read('/BlueValues [1 2 div 0] def\n/BlueValues [-20 0] def')
        assert masks == (1, 0, 0) and values[0] == [-20, 0]
        masks, values = read('/BlueValues [-20 0] def\n/Unused 1 dict begin /BlueValues [1 2] def end')
        assert masks == (1, 0, 0) and values[0] == [-20, 0]
        for prefix in ('/Unused << /BlueValues [1 2] >> def',
                       '/Unused [/BlueValues [1 2]] def',
                       '/Unused {/BlueValues [1 2] def} def'):
            masks, values = read(prefix + '\n/BlueValues [-20 0] def')
            assert masks == (1, 0, 0) and values[0] == [-20, 0]
        masks, values = read('/Define {readonly def} bind def /BlueValues [-20 0] Define')
        assert masks == (1, 0, 0) and values[0] == [-20, 0]
        masks, values = read('/ND {pop} def /BlueValues [-20 0] ND')
        assert masks == (1, 1, 1) and values[0] == []
        masks, values = read('/ND 1 def /BlueValues [-20 0] ND')
        assert masks == (1, 1, 1) and values[0] == []
        masks, values = read('/BlueValues [-20 0] def end dup /Private 10 dict dup begin /BlueShift 8 def')
        assert masks == (128, 0, 0) and values[0] == [] and values[7] == 8
        masks, values = read('/BlueValues [16#FFFFFFEC 0] def /BlueScale 4e-2 def')
        assert masks == (65, 0, 0) and values[0] == [-20, 0] and values[6] == .04
        masks, values = read('/Unused 1 dict begin /ND {pop} def end /BlueValues [-20 0] ND')
        assert masks == (1, 0, 0) and values[0] == [-20, 0]
        masks, values = read('/Unused 1 dict begin /Define {def} def end /BlueValues [-20 0] Define')
        assert masks == (1, 1, 1) and values[0] == []
        masks, values = read('end dup /Private 10 dict dup begin /BlueValues [-20 0] ND')
        assert masks == (1, 1, 1) and values[0] == []
        print(f'Type 1 private hints: {checks} authored checks passed; no grid fitting')


if __name__ == '__main__':
    main()
