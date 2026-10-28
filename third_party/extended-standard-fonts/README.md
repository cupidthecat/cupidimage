# Extended standard-font substitutes

These unmodified Liberation Fonts 2.1.5 programs provide glyphs absent from the
compact Latin substitutes. The manifest pins the official release archive and
checks every font, AUTHORS and LICENSE file by SHA-256.

`scripts/generate_pdf_extended_fonts.py` embeds losslessly compressed copies. The
renderer decompresses a face only when its selected encoding needs an extended
glyph. It uses the existing native TrueType reader and outline cache; no system
font lookup or additional runtime library is required. PDF widths and the standard
AFM metrics still control text placement.

The original copyright and SIL Open Font License 1.1 are preserved here and in
THIRD_PARTY_NOTICES.md, which must accompany binary distributions. Font names and
font bytes are unchanged. The fonts are not relicensed under the program's license.
