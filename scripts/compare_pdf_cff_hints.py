#!/usr/bin/env python3
"""Compare Type 2 hint operands and masks with an independent charstring decoder."""
import io
from pathlib import Path
import subprocess
import sys
import tempfile
from generate_pdf_standard_outlines import FONTS, ROOT, font_bytes

try:
    from fontTools.cffLib import CFFFontSet
    from fontTools.misc.psCharStrings import T2OutlineExtractor
    from fontTools.pens.recordingPen import RecordingPen
except ImportError:
    print('CFF hint operand comparisons skipped: FontTools unavailable')
    raise SystemExit(0)


class HintExtractor(T2OutlineExtractor):
    def __init__(self, char):
        private = char.private
        super().__init__(RecordingPen(), getattr(private, 'Subrs', []), char.globalSubrs,
                         private.nominalWidthX, private.defaultWidthX, private)
        self.axis = 0
        self.stems = []
        self.masks = []

    def countHints(self):
        args = self.popallWidth()
        self.hintCount += len(args) // 2
        end = 0
        for start, width in zip(args[::2], args[1::2]):
            start += end
            self.stems.append((self.axis, start, width))
            end = start + width

    def op_hstem(self, index):
        self.axis = 0
        self.countHints()

    op_hstemhm = op_hstem

    def op_vstem(self, index):
        self.axis = 1
        self.countHints()

    op_vstemhm = op_vstem

    def mask(self, index, counter):
        self.axis = 1
        value, index = super().op_hintmask(index)
        self.masks.append((self.hintCount, counter, value.hex()))
        return value, index

    def op_hintmask(self, index):
        return self.mask(index, 0)

    def op_cntrmask(self, index):
        return self.mask(index, 1)


def main():
    probe = str(Path(sys.argv[1]).resolve())
    fonts = [(name, font_bytes(stem)) for name, stem in FONTS.items()]
    fonts += [(p.name, p.read_bytes()) for p in sorted(
        (ROOT / 'third_party/standard-spacing-marks').glob('*.cff'))]
    glyphs = stems = masks = diagnostics = 0
    with tempfile.TemporaryDirectory(prefix='cupid-cff-hint-reference-') as tmp:
        path = Path(tmp) / 'font.cff'
        for name, data in fonts:
            path.write_bytes(data)
            lines = subprocess.check_output([probe, str(path), '--hint-outline'], text=True,
                                            timeout=30).splitlines()
            actual_stems, actual_masks = {}, {}
            for line in lines:
                fields = line.split()
                if fields[0] == 'hint-program':
                    gid = int(fields[1])
                    actual_stems[gid], actual_masks[gid] = [], []
                    diagnostics += bool(int(fields[3]))
                elif fields[0] == 'stem':
                    actual_stems[int(fields[1])].append(tuple(map(float, fields[2:])))
                elif fields[0] == 'hint-event':
                    actual_masks[int(fields[1])].append((int(fields[3]), int(fields[4]),
                                                         ''.join(fields[5:])))
            cff = CFFFontSet()
            cff.decompile(io.BytesIO(data), None)
            top = cff.topDictIndex[0]
            for gid, glyph in enumerate(top.charset):
                char = top.CharStrings[glyph]
                extractor = HintExtractor(char)
                extractor.execute(char)
                assert actual_stems[gid] == extractor.stems, (name, glyph, actual_stems[gid], extractor.stems)
                assert actual_masks[gid] == extractor.masks, (name, glyph, actual_masks[gid], extractor.masks)
                glyphs += 1
                stems += len(extractor.stems)
                masks += len(extractor.masks)
    print(f'CFF hint operands: {glyphs} glyphs, {stems} stems, {masks} masks match independent decoding; '
          f'{diagnostics} glyphs retain diagnostics')


if __name__ == '__main__':
    main()
