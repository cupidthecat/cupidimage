# Test assets and provenance

## Cross-format pixel references

Each supported input format has a small owned quadrant image in its `reference/`
subdirectory: PNG, baseline and progressive JPEG, lossless WebP, GIF, BMP, ICO,
CUR, TIFF, TGA, SVG, and PDF. The manifest in
`tests/fixtures/format-reference.json` records expected RGBA pixels and dimensions.
Lossless and vector expectations come from the original synthetic pixels. JPEG
uses independent Pillow/libjpeg output, with a maximum channel tolerance of 3 for
integer IDCT rounding. CUR also checks the cursor hotspot.

`make test-format-pixels` checks all 15 files through the public loader, then
independently decodes their ANSI, Kitty, and Sixel output back to pixels. These
are basic format and output checks, not full feature coverage. Existing focused
regressions cover additional decoding, animation, and terminal behavior.
Regenerate the files with `python3 scripts/generate_format_reference.py`; Pillow
is needed for generation, not for routine tests. All these synthetic fixtures
use the repository's license.

## Owned WebP images

`assets/webp/reference/` contains 49 actual WebP files generated from synthetic
constant, gradient, and deterministic random images. They include multiple
qualities, dimensions, and VP8 coefficient-partition counts. These fixtures
were generated for cupidimage and use the repository's license.

`tests/fixtures/vp8-reference.json` records their hashes, dimensions, generation
parameters, and expected decoded pixels. The normal tests read the actual files
under `assets/`. Regenerate them with `scripts/generate_webp_reference.py`.
Expected pixels use FFmpeg-decoded YUV, nearest-neighbor chroma, and the documented
limited-range BT.601 conversion. They do not claim equivalence to interpolated
chroma display modes.

## JPEG reference corpus

`assets/jpeg/reference/` also contains 72 grayscale and RGB 4:4:4 baseline and
progressive JPEGs. `make test-jpeg-reference` compares them with pinned libjpeg
pixels. Maximum channel tolerances are 2 for grayscale and 3 for RGB, accounting
for integer IDCT and color conversion rounding. No chroma interpolation tolerance
is hidden here: these RGB files have no subsampling. Regenerate them with
`scripts/generate_jpeg_reference.py` (Pillow required only for generation).

## PDF samples

`assets/pdf/pypdf/minimal-document.pdf` is an unchanged copy of
[001-trivial/minimal-document.pdf](https://github.com/py-pdf/sample-files/blob/02b8203bf416db3dbdb03438bc8278dfcb0555f3/001-trivial/minimal-document.pdf)
from **py-pdf/sample-files**, contributed by that project's contributors.
Upstream revision: `02b8203bf416db3dbdb03438bc8278dfcb0555f3`.

It is licensed under [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
The complete license is retained beside the PDF. This asset's license is separate
from the C library's license. No endorsement is implied.

`assets/pdf/pdfjs/` contains unchanged copies of PDF.js `basicapi.pdf`,
`empty.pdf`, and `asciihexdecode.pdf`, pinned to revision `67667f1e449378a2675f4095cded012b7ce5f37b`.
They are project test fixtures: a simple three-page table of contents, a blank
page, and an ASCIIHex text stream, rather than arbitrary third-party documents from the larger corpus.
The upstream Apache 2.0 license is retained beside them. `basicapi.pdf` embeds
DejaVu Sans font subsets; the DejaVu Bitstream/Arev license notices are retained
separately. These licenses do not change the library's license.

Sources and contribution history:
- [PDF.js test corpus at the pinned revision](https://github.com/mozilla/pdf.js/tree/67667f1e449378a2675f4095cded012b7ce5f37b/test/pdfs)
- [Basic API fixture contribution](https://github.com/mozilla/pdf.js/commit/3925e374177d9e5b8d470c92b8b23c31fdf11b4e)
- [Blank fixture contribution](https://github.com/mozilla/pdf.js/commit/85b38fc247d9b36b9e687469456d54f18c9bcd3e)
- [DejaVu font licensing](https://dejavu-fonts.github.io/License.html)

`make test-pdf-corpus` reads these four vendored files without network access.
It tests six pages, including exact white pixels for the empty page. It does not
establish complete PDF font or visual rendering support. The downloader remains
available for future manifest entries that are intentionally cache-only. Other
PDF.js documents need individual provenance review before vendoring.

## Layout and integrity

Assets are grouped by their actual format. CUR files share the ICO directory.
The inventory in `assets/manifest.json` pins every fixture and support file by
SHA-256. PNG checks also verify chunk bounds, CRCs, and palette size. After reviewing intended changes, refresh it with
`python3 scripts/update_asset_manifest.py`; tests never rewrite the inventory.
`make test-assets` checks inventory completeness, extensions, signatures,
SVG XML roots, and WebP container lengths. Signature checks do not prove complete
decoding or standards conformance.

Deliberately malformed PNG, BMP, ICO, and TGA samples live in each format's `negative/`
directory. The inventory describes each exception. These files must stay malformed;
they are not examples of valid images. TGA type 0 is a legitimate no-image header.

Three old files with `.webp` extensions contained PNGs. Their bytes are unchanged,
but they now live in `assets/png/` with PNG names. The old `16bit.png` was an 8-bit
RGB image and is now `rgb8-sample.png`; `16bitgrey.png` really is 16-bit grayscale.

Five TIFF files were incomplete generator placeholders. They now contain actual
packed 1-, 2-, or 4-bit images, generated by `scripts/repair_tiff_assets.py`. The
minimal big-endian TIFF's inline SHORT fields and the multipage fixture's accidental
64-bit floating-point samples have also been corrected. The multipage file now
contains three 8-bit grayscale pages. Existing legacy assets have not been given
new licensing claims; their original provenance still needs review.

The TIFF inline-value repair follows [TIFF 6.0, section 2](https://www.itu.int/itudoc/itu-t/com16/tiff-fx/docs/tiff6.pdf): values that fit inside the IFD value field start at its lowest address in either byte order.

The old 1-bit palette PNG had too many PLTE entries. Its valid replacement keeps
the same pixels and two usable colors; the original is preserved as an explicit
negative fixture.

The owned PDF references also cover an EOF marker without a final newline,
marked-content property dictionaries, and indirectly specified Flate stream
lengths. Their expected quadrant pixels are analytic. The old `minimal_xref.pdf`
had a 44-byte Length for 38 bytes of content and stale object offsets. Its corrected
replacement is generated with exact lengths/offsets; the original is retained as
`assets/pdf/negative/incorrect-stream-length.pdf`.

Five additional owned PDF image fixtures cover packed grayscale, packed RGB with
PNG prediction, 16-bit TIFF prediction, stencil masks, and color-key transparency.
Their full expected pixels are in `tests/fixtures/pdf-image-reference.json` and
are checked by the all-asset runner. Regenerate them with
`scripts/generate_pdf_image_reference.py`, which needs only Python's standard library.
