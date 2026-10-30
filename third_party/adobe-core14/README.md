# Standard PDF font metrics

These 14 Adobe AFM files and `readme.txt` are unmodified copies distributed by
Matplotlib 3.10.8. Every file matches its upstream Git blob at tag `v3.10.8`.
`manifest.json` records the source and SHA-256 hashes. The original readme
contains the distribution terms; retain it with the AFMs.

`scripts/generate_pdf_standard_metrics.py` generates a compact, immutable C
representation of the glyph widths and built-in character encodings. It does
not copy kerning pairs into PDF text layout. PDF text operators, not automatic
kerning or ligature substitution, control placement.

These resources contain metrics, not font outlines.
