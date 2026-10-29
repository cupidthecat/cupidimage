#!/usr/bin/env python3
"""Optional independent extended-font geometry checks; requires FontTools."""
import io
import math
from pathlib import Path
import subprocess
import sys
from fontTools import agl
from fontTools.cffLib import CFFFontSet
from fontTools.ttLib import TTFont
from generate_pdf_extended_fonts import FONTS, SOURCE
from generate_pdf_standard_outlines import FONTS as COMPACT, font_bytes
from generate_pdf_standard_metrics import metrics, SOURCE as AFMS

probe = str(Path(sys.argv[1]).resolve())
checks = supplemented = 0
for name, filename in sorted(FONTS.items()):
    compact = CFFFontSet()
    compact.decompile(io.BytesIO(font_bytes(COMPACT[name])), TTFont())
    primary = set(compact[compact.fontNames[0]].charset)
    named, _ = metrics(AFMS / (name + '.afm'))
    path = SOURCE / filename
    with TTFont(path) as font:
        order = font.getGlyphOrder()
        cmap = font.getBestCmap()
        for glyph_name in sorted(set(named) - primary):
            selected = glyph_name if glyph_name in order else None
            scalar = agl.toUnicode(glyph_name)
            if selected is None and len(scalar) == 1:
                selected = cmap.get(ord(scalar))
            if selected is None:
                assert glyph_name == 'commaaccent' and name.startswith(('Courier', 'Times'))
                supplemented += 1
                continue
            glyph = font['glyf'][selected]
            coordinates, ends, flags = glyph.getCoordinates(font['glyf'], round=lambda x: x)
            origin = glyph.xMin - font['hmtx'][selected][1]
            expected = [(x - origin, y, f & 1, int(i in ends))
                        for i, ((x, y), f) in enumerate(zip(coordinates, flags))]
            result = subprocess.run([probe, str(path), str(order.index(selected))],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, (name, glyph_name, result.stderr)
            lines = result.stdout.splitlines()
            actual = [tuple(map(float, row.split())) for row in lines[1:]]
            assert len(actual) == len(expected), (name, glyph_name, 'point count')
            assert all(math.isclose(a, b, abs_tol=1e-8) for aa, bb in zip(actual, expected)
                       for a, b in zip(aa, bb)), (name, glyph_name, 'outline mismatch')
            checks += 1
print(f'Independent extended-font geometry: {checks} outlines match; {supplemented} spacing-mark fallbacks tested separately')
