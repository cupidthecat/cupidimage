# CID-keyed Japanese substitute fonts

The two OpenType files are unmodified Harano Aji regular fonts from
https://github.com/trueroad/HaranoAjiFonts at revision
`f0f692c1e39593751a4796082fa5232185d91ecd`. Their hashes and Adobe-Japan1-7
collection identifiers are recorded in `manifest.json`.

The original files retain their font names, copyright notices and licensing
metadata. They are distributed under the SIL Open Font License 1.1 in LICENSE.
Include that license and the copyright notices in THIRD_PARTY_NOTICES.md with
binary distributions. The compressed copies restore the exact original bytes;
the font programs have not been subsetted, renamed or edited.

`scripts/generate_pdf_cid_fonts.py` packs each file independently. Fixed-size
string rows avoid huge numeric initializer trees and keep individual string
literals within the C99 minimum supported size. Only the last row is padded;
the recorded compressed length excludes that padding. Each font has its own
translation unit. Both compressed fonts together occupy about 9.1 MiB in the
binary.

These are regular-weight substitutes, not replacements with identical designs
for every Japanese typeface. Source-font glyph coverage limits still apply.
The renderer currently selects them only for nonembedded CIDFontType0 resources
whose collection is explicitly Adobe-Japan1 with supplement 0 through 7.
The same font bytes also provide Unicode cmap selection for the six supported
Japanese predefined encodings on nonembedded CIDFontType2 resources. Vertical
GID variants are extracted from each pinned font's single `vrt2` lookup, with
independent fontTools mapping checks. Other collections and encodings remain
separate work.
