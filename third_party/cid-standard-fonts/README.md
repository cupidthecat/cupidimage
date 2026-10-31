# CID-keyed CJK substitute fonts

These eight OpenType files are unmodified Harano Aji regular fonts. Each pinned
collection has a Gothic sans-serif face and a Mincho serif face:

- Adobe-Japan1-7: https://github.com/trueroad/HaranoAjiFonts
- Adobe-GB1-6: https://github.com/trueroad/HaranoAjiFontsCN
- Adobe-CNS1-7: https://github.com/trueroad/HaranoAjiFontsTW
- Adobe-Korea1-2: https://github.com/trueroad/HaranoAjiFontsK1

Exact source revisions, byte counts, hashes, collection identifiers and copyright
notices are recorded in manifest.json. Upstream labels the Chinese and Korean
variants experimental. Source-font glyph coverage and regular-weight designs do
not provide identical substitutes for every CJK typeface.

The original files retain their names, copyright notices and licensing metadata.
They are distributed under the SIL Open Font License 1.1 in LICENSE. Include that
license and the copyright notices in THIRD_PARTY_NOTICES.md with binary
distributions. The compressed copies restore the exact original bytes; the font
programs have not been subsetted, renamed or edited.

scripts/generate_pdf_cid_fonts.py packs each file independently. Fixed-size
string rows keep individual literals within the C99 minimum supported size.
Only the last row is padded, and its padding is excluded from the recorded
compressed length. Each face has its own translation unit. The compressed font
bytes together occupy about 43.7 MiB in the binary.

The renderer uses collection-matched CID selection for nonembedded CIDFontType0
resources. Selected Unicode predefined CMaps also support character-code/cmap
selection for external CIDFontType2 fonts. Vertical GID variants are extracted
from each unchanged font's single vrt2 lookup and checked against an independent
fontTools oracle. See ../../docs/pdf-fonts.md for encodings, limits and tests.
