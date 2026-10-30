# Test assets

Images and documents are organized by format: `png`, `jpeg`, `webp`, `gif`,
`bmp`, `ico` (including CUR), `tiff`, `tga`, `svg`, and `pdf`.

- `<format>/reference/`: owned, known-pixel fixtures for every supported input format.
- `webp/reference/`: includes 49 lossy VP8 references plus a lossless image.
- `pdf/pdfjs/`: pinned Mozilla PDF.js samples with project and font license notices.
- `pdf/pypdf/`: a pinned upstream PDF with its own license.
- `<format>/negative/`: intentionally malformed or nonstandard test input.
- `manifest.json`: file inventory, hashes, and explanations of negative fixtures.

Run `make test-assets` to catch mislabeled, changed, missing, or unlisted files.
Run `make test-format-pixels test-webp-pixels test-pdf-corpus` for the reference image/page tests.
See [provenance and validation details](../docs/test-assets.md) before reusing assets.

The standard spacing-mark sample exercises all eight Courier/Times variants and
the compact/extended/mark fallback chain. Its raster is unverified. The companion
`pdf/reference/standard-spacing-advance.pdf` checks AFM-controlled placement with
an analytic marker oracle. Regenerate them with `scripts/generate_pdf_spacing_fixtures.py`.

`pdf/text-coverage.pdf` is an owned geometric/standard-font sample for the opt-in
text coverage path. Its complete raster is decoded-unverified. Rebuild it with
`scripts/generate_pdf_text_coverage_fixture.py`; the separate page coverage tests
use analytic areas and independent curve masks.

`pdf/native-truetype-hinting.pdf` embeds an owned instruction font. Its glyphs move
outline points and a phantom advance, while PDF widths retain control of spacing.
The full sample is decoded-unverified. Regenerate it with
`scripts/generate_pdf_page_hinting_fixture.py`; its font source is in
`scripts/generate_pdf_page_hinting_font.py`.

`pdf/native-cff-hinting.pdf` embeds owned blue-zone and composite glyph programs.
Use `--pdf-cff-hinting --pdf-text-aa` to exercise their fitted page path. Its
complete raster remains decoded-unverified. Regenerate it with
`scripts/generate_pdf_cff_page_hinting_fixture.py`.

`pdf/native-type1-hinting.pdf` embeds owned Type 1 blue-zone and composite glyph
programs. Use `--pdf-type1-hinting --pdf-text-aa` to exercise their fitted page
path. Its complete raster remains decoded-unverified. Regenerate it with
`scripts/generate_pdf_type1_page_hinting_fixture.py`.

`pdf/type1-counter-hints.pdf` exercises chunked Type 1 counter metadata with two
horizontal stems. The metadata no longer prevents rendering; designer counter
priorities remain unapplied. Its full raster is decoded-unverified. Regenerate it
with `scripts/generate_pdf_type1_counter_fixture.py`.
