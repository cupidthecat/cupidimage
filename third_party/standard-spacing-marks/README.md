# Standard spacing-mark subsets

These eight renamed CFF subsets supply the standalone `commaaccent` missing from
the compact and extended Courier/Times substitutes. The source glyph is
`space_uni0326` in TeX Gyre Cursor and Termes 2.004. The upstream character charts
identify that spacing glyph as `commaaccent`:

- https://mirrors.ctan.org/fonts/tex-gyre/doc/qcr-info.pdf
- https://mirrors.ctan.org/fonts/tex-gyre/doc/qtm-info.pdf

The outlines and their original widths are unchanged. PDF Widths, MissingWidth or
the standard AFM metrics still determine text advances. In particular, a source
font's spacing width does not replace the PDF standard-font width.

## Changes and provenance

`scripts/generate_pdf_standard_marks.py` uses FontTools 4.61.1 to retain `.notdef`
and `space_uni0326`, prune unused glyph/subroutine data, and give each subset a
`CupidImageSpacing-` name. It replaces the CFF FullName/FamilyName and appends a
change notice to the original Notice. Those are the only intended changes;
rebuilding checks the retained glyph's drawing commands and width before and
after subsetting. The C99 arrays are a byte-preserving representation of these
CFF files, not newly designed outlines.

`manifest.json` records original OTF URLs and hashes, subset names and hashes,
the generator version and the complete upstream archive. The `UPSTREAM-*` files
are unchanged copies of the original manifests and readmes. They describe the
complete upstream works, not the smaller selection distributed here. Complete
original sources remain available from:

- https://www.gust.org.pl/projects/e-foundry/tex-gyre
- https://mirrors.ctan.org/fonts/tex-gyre.zip

Download the eight original OTFs listed in the manifest into a private directory,
then reproduce the subsets and generated declarations with:

    python3 scripts/generate_pdf_standard_marks.py --rebuild /path/to/original-otfs
    python3 scripts/generate_pdf_standard_marks.py --check

No download or FontTools installation is required to build CupidImage or check
its generated declarations. The optional original-program tests take the same
source directory via `--sources`.

## License and distribution

The fonts remain under the GUST Font License, an instance of LPPL 1.3c or later.
The publisher's GFL 1.0 and LPPL texts are included unchanged. The older preliminary
GFL text shipped in the upstream archive is also retained as `UPSTREAM-GUST-FONT-LICENSE.txt`. The upstream authors retain their
copyright; the font data is not relicensed under CupidImage's code license.
CupidImage maintains these derived subsets. The original authors do not provide
support for CupidImage's changes.

When distributing binaries containing these subsets, include this directory,
`scripts/generate_pdf_standard_marks.py`, the generated
`src/formats/pdf/fonts/standard_marks.inc` and THIRD_PARTY_NOTICES.md. This keeps
the derived font work, change record, licenses and original-source information
available to recipients alongside the compiled representation.
