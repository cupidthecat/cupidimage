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


`pdf/fractional-clipping.pdf` shows clipped outlines, a fractional Form boundary
and repeated narrow clips. Use `--pdf-text-aa` to exercise fractional coverage.
Its complete raster is decoded-unverified. Regenerate it with
`scripts/generate_pdf_fractional_clip_fixture.py`.

`pdf/graphics-coverage.pdf` shows fractional path fills, styled strokes, curves,
combined painting and clipped Type 3 glyphs. Its full raster is decoded-unverified.
Rebuild it with `scripts/generate_pdf_graphics_coverage_fixture.py`; use
`--pdf-graphics-aa` to inspect the antialiased path profile.

`pdf/image-interpolation.pdf` compares nearest and bilinear image samples, a
Type 3 stencil glyph and an independent soft mask. It is decoded-unverified.
Regenerate it with `scripts/generate_pdf_image_interpolation_fixture.py`.

`pdf/cie-blending.pdf` compares device and ICC blending, wide-gamut color
transport and CIE luminosity masks. Its full raster is decoded-unverified.
Regenerate it with `scripts/generate_pdf_cie_blend_fixture.py`.

`pdf/icc-destination-blending.pdf` exercises XYZ/Lab reverse lookup tables and
multistage ICC destination transforms. The authored profiles test pipeline
behavior, not physical devices. Its complete raster is decoded-unverified.
Regenerate it with `scripts/generate_pdf_icc_destination_fixture.py`.

The owned pdf/cmyk-blending.pdf fixture exercises native four-component opacity,
nonseparable blending, black generation and ICC groups. Its full raster is
decoded-unverified. Regenerate it with scripts/generate_pdf_cmyk_blend_fixture.py.

The nested-mask-surfaces.pdf fixture is deliberately a plain red page: four
levels of isolated masks must preserve every pixel within the existing memory
limit. The clipped-shading-work.pdf fixture repeatedly paints a small clipped
region without evaluating hidden tint colors. The nested-mask page has an exact
full-raster oracle, independently matched at 96 DPI. The clipped shading page
remains decoded-unverified because the independent device-color conversion
differs. Regenerate them with
scripts/generate_pdf_group_work_fixtures.py.

The dense-vector-path.pdf and localized-clip-work.pdf fixtures exercise large
compound paths and repeated small clips. Both have literal native pixel oracles.
The dense path also matches an independent 96-DPI raster; the clip has a documented
antialiased boundary difference. See ../docs/pdf-dense-paths.md. Regenerate them
with scripts/generate_pdf_dense_path_fixtures.py.

The repeated-page-clips.pdf fixture applies 7,072 page rectangles over a smaller
inherited clip. It has a literal red-and-white pixel oracle and uses the same
dense-path fixture generator.

The bounded-shading-work.pdf fixture repeats an opaque shading inside a small
clip on a letter-sized page. Its native raster has a literal green-rectangle
oracle. It is generated by scripts/generate_pdf_dense_path_fixtures.py.

The optional-content-state.pdf reference asset uses default group states, a
membership policy and a visibility expression to select three opaque colour
bands. Its literal pixel oracle also matches an independent 96-DPI raster.
The optional-content-text.pdf sample shows preserved advances and glyph clipping
inside hidden layers; its complete raster is decoded-unverified. Regenerate both
with scripts/generate_pdf_optional_fixtures.py.

The subset-cid-truetype.pdf and subset-cid-cff.pdf reference assets contain owned
CID fonts with deliberately damaged subset tables. Their selected glyphs retain
the same blue rectangle as valid controls. Both have exact literal pixel oracles
and independent 96-DPI matches. Regenerate them with
scripts/generate_pdf_subset_fixtures.py; see ../docs/pdf-subset-fonts.md for the
bounded recovery policy.

The annotation-appearance.pdf and annotation-widget-state.pdf reference assets
paint the same literal blue rectangle through a normal appearance and an AS-selected
widget state. They exercise BBox-to-Rect placement without external fonts. Generate
them with scripts/generate_pdf_annotation_fixtures.py.

The cff-direct-cid.pdf and opentype-cff-direct-cid.pdf reference assets select glyph
index 2 from name-keyed CFF programs through Type 0 PDF fonts. Both have literal
blue-rectangle pixel expectations. Generate them with
scripts/generate_pdf_cff_binding_fixtures.py.

`pdf/reference/jpx-native-rgb.pdf` and `jpx-palette-opacity.pdf` exercise native
JPEG 2000 samples, palette expansion and embedded alpha. Both have literal pixel
oracles. Regenerate them with `scripts/generate_pdf_jpx_fixtures.py`.

The cid-substitute-gb1.pdf, cid-substitute-cns1.pdf and cid-substitute-korea1.pdf
samples exercise regular serif/sans Chinese and Korean substitutes, with
horizontal CIDFontType0 and vertical Unicode CIDFontType2 text. Their `-legacy.pdf`
companions exercise CP936, CP950 and CP949. They remain decoded-unverified visual
assets. Regenerate them with
scripts/generate_pdf_cjk_fixtures.py; paired embedded-program tests are separate.

The standalone-catalog-size.pdf fixture has a deliberately undersized trailer
bound but an explicitly indexed catalog. It has a literal red-page oracle for
the narrow standalone recovery policy. Regenerate it with
scripts/generate_pdf_catalog_size_fixture.py; see ../docs/pdf-objects.md for the
revision and null-semantics guards.

The post-aat4-names.pdf and post-high-index-names.pdf samples use owned glyph
outlines with legacy character-code names and modern high custom-name indices.
They remain decoded-unverified. Regenerate them with
scripts/generate_pdf_post_table_fixtures.py; separate paired tests verify glyph
selection and PDF widths.

`pdf/native-monochrome-hinting.pdf` uses owned narrow-stem glyphs to show simple,
smart and disabled dropout policies, composite overrides and conditional transforms.
View it with `--pdf-tt-hinting` and without `--pdf-text-aa`. The complete sample is
decoded-unverified; focused analytic and independent raster tests are described in
[monochrome rendering](../docs/pdf-monochrome.md).
The security-rc4.pdf, security-aes128.pdf and security-aes256.pdf assets have empty
user passwords and synthetic owner credentials. Each has a literal red-page
oracle and a nonzero content-object generation. They test encrypted loading
through the existing default APIs. Regenerate their exact bytes with
scripts/generate_pdf_security_assets.py. The larger password/structure fixtures
and independent generation procedure are described in ../docs/pdf-security.md.
