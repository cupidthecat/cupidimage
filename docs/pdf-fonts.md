# PDF fonts and text

PDF text is still a work in progress. Supported embedded TrueType and CFF fonts use
their outlines instead of the bitmap fallback. This is unhinted rendering, not a
claim of complete font support or identical pixels across rasterizers.

## Glyph selection comes before Unicode

A simple TrueType font consumes one encoded byte per glyph. StandardEncoding,
MacRomanEncoding and WinAnsiEncoding provide base glyph names; Differences can
override them. Names are mapped through the pinned Adobe Glyph List and the
font's Unicode or Macintosh cmap. Symbolic fonts use their symbol cmap code
ranges, with the documented fallback when the expected cmap is absent. Custom
version-2 post names and its standard ASCII names are also available.

Identity-H/V fonts consume two-byte CIDs. Embedded encoding CMaps can consume
one to four bytes and map those source codes to CIDs. CIDToGIDMap can be Identity or a filtered
stream of big-endian glyph indices. Unmapped indices select glyph zero. ToUnicode
does not replace this glyph selection or determine glyph advances. Its maps
still supply characters for the bitmap fallback when no supported outline is
available.

The distinction matters: changing a ToUnicode entry must not silently replace a
TrueType glyph with a different outline. The tests cover that case, including a
malformed multibyte ToUnicode entry attached to a single-byte font.

## Embedded encoding CMaps

Encoding CMaps and ToUnicode maps serve different purposes. The encoding map
selects a CID; ToUnicode still looks up the original source code and its byte
length. Word spacing applies only to a one-byte source code 32, including in
vertical writing. A two-byte code 0020 does not receive word spacing.

Embedded maps support codespace ranges, cidchar/cidrange, notdefchar/notdefrange,
and UseCMap inheritance from another stream or Identity-H/V. Codespace matching
checks each byte against its corresponding bounds. Later local mappings override
earlier mappings and inherited entries. A notdef range maps every code to the
same CID, unlike an ordinary range. Missing glyphs consult the notdef map, then
CID zero. Invalid or truncated codes consume bytes using the longest partial
codespace match, with shorter ranges winning ties.

The stream dictionary's writing mode must agree with an explicit mode in its
program. Without an explicit mode, it defaults to horizontal; inheriting a
vertical base map does not set the derived map's writing mode.

Maps allow 256 non-overlapping codespaces, 65,536 mapping ranges per stream,
five levels including the root, and 4 MiB of decoded program data per inheritance
chain. Retained maps count against the CID metadata budget. Disjoint ranges use
binary search; overlapping definitions preserve last-definition precedence and
use a bounded scan. All text on a page shares a mapping-work budget.

All 59 predefined CJK maps listed in ISO 32000-1 Table 118 are bundled, alongside
Identity-H/V. Their horizontal and vertical mappings cover the Adobe CNS1, GB1,
Japan1 and Korea1 collections, including mixed-width and UTF-16 codes. They select
CIDs, not replacement font outlines: faithful rendering still requires a supported
font program. Other named maps remain unsupported. This parser reads the CMap
data format; it is not a general PostScript interpreter. Invalid maps retain the
font table's existing fallback policy rather than establishing faithful text.

## TrueType programs and geometry

FontFile2 accepts TrueType programs. FontFile3 accepts OpenType programs with
glyf outlines. The reader checks table ranges, short/long loca offsets, head,
maxp and horizontal metrics. Supported cmap forms are 0, 4, 6, 12 and 13.

Simple glyphs decode contour ends, repeated flags and signed coordinate deltas.
Composite glyphs support translation, affine component transforms, point
attachment, scaled/unscaled offsets and nested components. Cyclic components
fail. An obsolete reserved composite flag found in a corpus document is ignored;
its presence does not change the known component structure.

Consecutive off-curve points imply on-curve midpoints, including at the contour
boundary. Fill and clipping intersect the original quadratic curves directly.
Stroke coverage uses bounded adaptive tessellation and the shared stroke engine.
All eight text rendering modes use the text matrix, current transform, font size,
horizontal scaling and rise. Closed glyph contours retain their winding, so
holes and reflected clipping overlaps work without replacing glyphs by boxes.

PDF Widths/MissingWidth and CID metrics control positioning. The font's hmtx
advance is used for the existing fallback case where a simple font omits those
PDF metrics. That fallback is not a substitute for correctly authored font
metadata. Identity-V origin offsets and advances are applied before/after glyph
painting, respectively.

## CFF and Type 2 programs

FontFile3 accepts Type1C, CIDFontType0C and OpenType programs containing a CFF
version-1 table. The parser handles INDEX and DICT data, standard/custom strings,
all three charset forms, standard/expert/custom encodings and supplemental
encoding entries. CID fonts use charset CIDs, FDArray, FDSelect formats 0/3 and
the selected Private DICT. PDF encoding overrides and Differences select names
for simple fonts; ToUnicode does not replace those glyphs.

Type 2 programs support line and cubic path operators, local/global subroutines,
widths, arithmetic, stack manipulation, transient storage, conditionals and flex
curves. Subroutine bias depends on the number of subroutines. Stem declarations
and mask bytes are consumed, but grid fitting and flex flattening are not applied.
These remain unhinted outlines. The obsolete dotsection operator is a no-op.
An empty hint mask found in a cached font is accepted without consuming bytes;
this is malformed-input recovery, not valid hint syntax.

The deprecated endchar accent form resolves component names through
StandardEncoding, regardless of the PDF encoding. Nested accent composites are
rejected. FontMatrix and per-FD matrices transform the outlines before text
placement. Fill and clipping intersect the original cubic curves; stroke paths
use bounded adaptive subdivision. All eight PDF text rendering modes share the
existing compositing, stroke and text-clipping rules.

A single-font CFF program may contain up to 65,535 glyphs, 256 font dictionaries
and 64,609 nonstandard strings. Each charstring is limited to 65,535 bytes,
48 stack entries, 32 transient values, 96 stem hints, ten subroutine nesting
levels and 65,536 interpreter steps. Outlines are bounded to 65,536 segments.
Malformed programs fail cleanly; these limits do not establish compatibility
with every font in the broader corpus.

## Bounds and ownership

Each retained font program is limited to 16 MiB, with 64 MiB of program storage
per collected font table. A CIDToGIDMap is at most 131,072 bytes. Glyph decoding
allows 65,536 points, 4,096 contours, 4,096 component calls and 16 nested component
levels. Stroked outlines share the path's 4,096-segment bound. Pixel intersection
work and text clipping use the page's shared budgets.

Programs, maps, component points, curve edges and stroke masks are released on
both success and failure. Invalid metadata retains the existing font-table
fallback policy; an invalid glyph encountered in a loaded program fails the page
rather than quietly dropping that glyph.

## What remains

- TrueType instruction execution, grid fitting and antialiased font coverage.
- Non-CFF Type 1 programs, CFF2, synthetic/multiple-master fonts and variable-font data.
- Complete post-table name variants and the remaining cmap forms.
- Complete CJK font rendering beyond the supported encodings and embedded programs.
- Complete standard-font metrics and faithful outlines when fonts are not embedded.

These gaps can still change glyph shape, pixel phase or text coverage. Successful
loading is not enough to mark a font or document correct.

## Tests and provenance

`make test-pdf-cmaps` runs 68 encoding, recovery, inheritance and rendering checks.
Seven selected cases also compare exact pixels with an independent renderer:

    python3 scripts/test_pdf_cmaps.py bin/test_pdf_pages bin/test_pdf_cmap --reference

`make test-pdf-predefined-cmaps` checks all 59 bundled resources and 885,128
character mappings against their pinned source definitions. That count includes
inherited and shared mappings; it is not a count of independent rendering
features. Six authored PDF cases cover the four collections, vertical selection
and a four-byte UTF-16 code. Optional exact raster comparisons run with:

    python3 scripts/test_pdf_predefined_cmaps.py bin/test_pdf_cmap bin/test_pdf_pages --reference

The allocation-failure suite exercises embedded and predefined encoding maps,
including inherited maps and cleanup after decompression or parsing failures.


`make test-pdf-cff` runs 56 data-primitive checks, 186 structure/truncation checks,
152 Type 2 checks and 30 PDF rendering checks. The rendering tests cover cubic
geometry, CID fonts, encoding overrides, accents, FontMatrix, all text modes and
OpenType wrapping. Twenty-two optional independent comparisons use:

    python3 scripts/test_pdf_cff_render.py bin/test_pdf_pages --reference

The clip-only comparison uses a second rasterizer because the first applies a
different boundary rule. The cubic arch also has an analytic per-pixel test.
The optional font-fixture regeneration script uses FontTools; ordinary tests do
not require it. The PDF allocation-failure suite has 568 checks, including
CFF font dictionaries, outlines and composite cleanup.

`make test-pdf-truetype` runs binary-table/glyph-name checks and 45 PDF rendering
checks. The 4,356 lower-level checks include the full pinned glyph-name dataset;
they are not thousands of independent font-rendering features. The authored
geometric font covers holes, quadratic curves, off-curve contour starts, affine
components and point attachment. Regular tests need only Python's standard
library. FontTools is needed only to regenerate that original test fixture.

For 19 optional independent raster comparisons, run:

    python3 scripts/test_pdf_embedded_fonts.py bin/test_pdf_pages --reference

The selected comparison fixtures separate outline behavior from fractional-pixel
hinting differences. Curve fills also have analytic pixel expectations. One
clipping comparison uses a second rasterizer because the first uses a different
boundary rule. No whole-page visual-parity claim follows from these checks.

The Adobe Glyph List is pinned under `third_party/adobe-glyph-list`, with its
copyright and BSD-3-Clause notice retained. [Third-party notices](../THIRD_PARTY_NOTICES.md)
must accompany binary distributions. The 59 predefined CMaps are pinned under
`third_party/adobe-cmaps`, with their original BSD notices and per-file hashes.
Base encoding tables follow ISO 32000-1
Annex D.2, including its duplicate-space, soft-hyphen and currency rules.

## Standards

- Adobe Technical Notes [5176: CFF](https://adobe-type-tools.github.io/font-tech-notes/pdfs/5176.CFF.pdf)
  and [5177: Type 2](https://adobe-type-tools.github.io/font-tech-notes/pdfs/5177.Type2.pdf)
  define the compact font structures, standard tables and charstring operators.

- [ISO 32000-1](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf),
  sections 9.3 through 9.7 and Annex D, defines PDF text state, fonts and encodings.
- The OpenType [glyf](https://learn.microsoft.com/en-us/typography/opentype/spec/glyf),
  [loca](https://learn.microsoft.com/en-us/typography/opentype/spec/loca),
  [head](https://learn.microsoft.com/en-us/typography/opentype/spec/head) and
  [cmap](https://learn.microsoft.com/en-us/typography/opentype/spec/cmap) specifications
  define the embedded font structures used here.
- [RFC 7995](https://www.rfc-editor.org/info/rfc7995/) discusses PDF publication
  requirements for RFCs, including embedded fonts and Unicode preservation.
  It is an application profile, not a replacement for the complete PDF
  specifications. This renderer does not claim PDF/A or PDF/UA validation.
