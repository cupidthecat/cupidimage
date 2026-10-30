#!/usr/bin/env python3
"""Compare Type 1 hint operand streams with an independent charstring decoder."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from compare_pdf_type1_hints import pfb_program


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    parser.add_argument('fonts', type=Path, nargs='+')
    args = parser.parse_args()
    from fontTools.misc.psCharStrings import T1OutlineExtractor
    from fontTools.pens.basePen import NullPen
    from fontTools.t1Lib import T1Font

    class Capture(T1OutlineExtractor):
        def reset(self):
            super().reset()
            self.sby = 0
            self.events = []

        def event(self, kind, axis, values):
            self.events.append((kind, axis, *values, *((0,) * (6 - len(values)))))

        def stem(self, kind, axis):
            values = self.popall()
            assert len(values) == (2 if kind == 0 else 6)
            values[::2] = [v + (self.sbx if axis else self.sby) for v in values[::2]]
            self.event(kind, axis, values)

        def op_hstem(self, index):
            self.stem(0, 0)

        def op_vstem(self, index):
            self.stem(0, 1)

        def op_hstem3(self, index):
            self.stem(1, 0)

        def op_vstem3(self, index):
            self.stem(1, 1)

        def op_sbw(self, index):
            self.sbx, self.sby, self.width, _ = self.popall()
            self.currentPoint = self.sbx, self.sby

        def op_dotsection(self, index):
            assert not self.popall()
            self.event(3, 0, [])

        def op_callothersubr(self, index):
            count, subr = self.operandStack[-2:]
            if subr == 3:
                assert count == 1
                self.event(2, 0, self.operandStack[-3:-2])
            elif subr == 0:
                assert count == 3
                self.event(4, 0, self.operandStack[-5:-2])
            super().op_callothersubr(index)

    paths = sorted({p for item in args.fonts for p in
                    (item.glob('*.pfb') if item.is_dir() else [item])})
    if not paths:
        parser.error('No PFB fonts selected')
    glyphs = events = 0
    with tempfile.TemporaryDirectory(prefix='cupid-type1-trace-reference-') as tmp:
        path = Path(tmp) / 'font.bin'
        for font in paths:
            data, clear, encrypted = pfb_program(font.read_bytes())
            path.write_bytes(data)
            out = subprocess.run([str(args.probe.resolve()), str(path), str(clear),
                                  str(encrypted), '--hint-outline'], capture_output=True,
                                 text=True, check=True, timeout=30)
            names, traces = {}, {}
            for line in out.stdout.splitlines():
                tokens = line.split()
                if tokens[0] == 'glyph':
                    names[int(tokens[1])] = tokens[2]
                elif tokens[0] == 'hint-trace':
                    traces.setdefault(int(tokens[1]), [])
                elif tokens[0] == 'hint-event':
                    glyph = int(tokens[1])
                    # Segment positions have authored tests; this compares decoded operands.
                    traces.setdefault(glyph, []).append(tuple(map(float, [tokens[2], *tokens[4:]])))
            independent = T1Font(str(font))
            independent.parse()
            for glyph, name in names.items():
                charstring = independent.font['CharStrings'][name]
                capture = Capture(NullPen(), charstring.subrs)
                capture.execute(charstring)
                assert traces[glyph] == capture.events, (font, name, traces[glyph], capture.events)
                glyphs += 1
                events += len(capture.events)
    print(f'Type 1 hint trace reference: {glyphs} glyphs, {events} events in {len(paths)} fonts; '
          'decoded operands only, no grid fitting')


if __name__ == '__main__':
    main()
