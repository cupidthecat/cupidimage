# Standard PDF substitute outlines

These unmodified source arrays contain compact CFF programs for 14 standard
PDF font substitutes. The pinned revision, upstream location and SHA-256 hashes
are recorded in `manifest.json`. Original copyright headers, AUTHORS and LICENSE
are retained. The font-array headers specify the BSD-style license; the upstream
LICENSE file is preserved in full.

`scripts/generate_pdf_standard_outlines.py` changes only the C++ declarations to
C99 data arrays. The font bytes are unchanged. The PDF renderer uses Adobe's
standard-font metrics separately from these substitute outlines.

Symbol and ZapfDingbats include every named glyph in the pinned AFMs. The Latin
faces cover the common Western encodings but omit 84 extended AFM names per
Courier face and 86 per Helvetica/Times face. They are substitutes, not the
original proprietary outlines, and are currently rendered without hinting. The
renderer now uses the separately licensed programs in
`third_party/extended-standard-fonts` for 1,016 of those missing face/glyph
combinations; this directory and its original font bytes are unchanged.
