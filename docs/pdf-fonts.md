# PDF fonts and text

PDF text is still a work in progress. Supported embedded TrueType, CFF and Type 1 fonts use
their outlines instead of the bitmap fallback. This is unhinted rendering, not a
claim of complete font support or identical pixels across rasterizers.

## Glyph selection comes before Unicode

A simple TrueType font consumes one encoded byte per glyph. StandardEncoding,
MacRomanEncoding and WinAnsiEncoding provide base glyph names; Differences can
override them. Names are mapped through the pinned Adobe Glyph List and the
font's Unicode or Macintosh cmap. Symbolic fonts use their symbol cmap code
ranges, with the documented fallback when the expected cmap is absent. The full 258-name Macintosh post ordering, custom version-2 names and legacy
version-2.5 signed offsets are also available. Version 1 requires the standard
258-glyph ordering; version 3 has no glyph names.

Identity-H/V fonts consume two-byte CIDs. Embedded encoding CMaps can consume
one to four bytes and map those source codes to CIDs. CIDToGIDMap can be Identity or a filtered
stream of big-endian glyph indices. Unmapped indices select glyph zero. ToUnicode
does not replace this glyph selection or determine glyph advances. Its maps
still supply characters for the bitmap fallback when no supported outline is
available.

The distinction matters: changing a ToUnicode entry must not silently replace a
TrueType glyph with a different outline. The tests cover that case, including a
malformed multibyte ToUnicode entry attached to a single-byte font. An invalid
ToUnicode object, filter or map is discarded without discarding an otherwise
valid outline program, Type 3 font or CID metrics. Partial Unicode mappings
are cleared on failure. Named encodings cannot read Differences from unrelated
dictionaries later in the font object.

## Type 3 encodings

Type 3 glyph selection uses the shared simple-font encoding resolver. Named
WinAnsi, MacRoman and MacExpert tables, BaseEncoding dictionaries and Differences
map character codes to CharProcs names. StandardEncoding is also accepted for
compatibility. A later mapping to a missing name clears any earlier mapping for
that code; a missing CharProcs name paints nothing.

Font Subtype names, including Type 0 descendant types and embedded CFF stream
types, may be indirect objects. They use the same bounded name resolver as font
encodings. The Type 3 suite has 60 checks and 40 optional raster comparisons;
the existing shared-stream boundary-tolerance case remains documented in the
[testing guide](testing.md#type-3-glyph-programs).

## Font selection through graphics state

An ExtGState Font entry can select a font and size without Tf. It contains a
font-dictionary reference and a text-space size, rather than a resource name.
The collector loads these referenced fonts in the same resource scope as named
fonts, including forms, tiling cells and Type 3 glyphs. Their cache keys are
object references, kept separate from resource names. Reusing one font with a
different size does not duplicate its program.

Font arrays and sizes may be indirect, including in compressed object streams.
A null Font entry leaves the current font and size unchanged.
The resulting font and size persist across BT/ET and are saved by q/Q like Tf
state. Invalid arrays, missing font dictionaries and cyclic size references are
rejected. Escaped names in Font and ExtGState resources are decoded consistently.

`make test-pdf-graphics-fonts` runs 31 scope, state and malformed-input tests.
Nineteen optional exact comparisons use:

    python3 scripts/test_pdf_graphics_fonts.py bin/test_pdf_pages --reference

The tiling-cell check compares ExtGState selection with Tf within the same
pattern renderer; it does not establish fractional-cell raster fidelity.

## Standard 14 fonts

Nonembedded Type 1 fonts with one of the 14 canonical PDF font names use the
original Adobe AFM widths. The built-in encodings include Symbol and
ZapfDingbats. Explicit base encodings and Differences select named metrics;
undefined names have zero advance. Explicit PDF Widths and MissingWidth still
take precedence. No kerning pairs or automatic ligatures are applied.

These simple fonts consume one byte per character, even when a malformed
ToUnicode map describes longer codes. ToUnicode cannot change their advances.
Their outlines come from bundled, BSD-licensed CFF substitutes, not the bitmap
fallback. Symbol and ZapfDingbats cover all named glyphs in the pinned AFMs.
The compact Courier faces omit 84 extended AFM names and Helvetica/Times omit
86. An additional pinned TrueType face now supplies 1,016 of these 1,024 missing
face/glyph combinations. Existing compact outlines keep priority. The standalone
commaaccent remains missing in the four Courier and four Times faces; these eight
combinations retain .notdef while preserving the specified advance. There is no claim that these substitute shapes are identical
to every platform's font choice. Hinting remains unimplemented.

The pinned AFMs, original license, provenance and per-file hashes are in
`third_party/adobe-core14`. The generated tables require no installed fonts or
runtime filesystem access. Substitute programs and their original notices are
pinned under `third_party/standard-fonts`; their generated representation keeps
the original font bytes unchanged.

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
data format; it is not a general PostScript interpreter. Invalid required encoding
maps fail font loading. Optional ToUnicode extraction maps are handled separately.

## TrueType programs and geometry

FontFile2 accepts TrueType programs. FontFile3 accepts OpenType programs with
glyf outlines. The reader checks table ranges, short/long loca offsets, head,
maxp and horizontal metrics. Supported cmap forms are 0, 2, 4, 6, 8, 10, 12 and 13. Format 2 handles
single-byte characters, lead-byte subheaders, shared glyph arrays and signed
deltas without changing missing-glyph zero. Formats 8 and 10 accept full-width
character codes. Declared lengths, offsets and glyph IDs remain bounded.
These font cmaps do not replace the PDF encoding CMap that consumes text bytes.

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

### Horizontal glyph origins

TrueType outlines use the horizontal pen origin derived from xMin and the signed
left side bearing in hmtx. The bearing is read from either a long metric record
or the trailing side-bearing array. The complete glyph is shifted into pen
coordinates only after its components have been assembled. Shifting each child
first would break component offsets and point attachment.

USE_MY_METRICS carries the selected component's horizontal origin through nested
composites. PDF Widths and CID metrics still determine text advances; moving an
outline does not change the next character's position. Cached outlines retain
this font-space normalization across text transforms, fill/stroke and clipping.
This does not implement hint instructions, phantom-point attachment or grid fitting.

The origin and metric rules follow the OpenType
[hmtx](https://learn.microsoft.com/en-us/typography/opentype/spec/hmtx) and
[glyf](https://learn.microsoft.com/en-us/typography/opentype/spec/glyf) tables.
`make test-pdf-truetype-bearings` checks signed bearings, nonzero xMin, both metric
layouts, composite metric selection, nested components, PDF width overrides,
clipping and allocation recovery. Two owned PDF assets have analytic pixel
references. The optional independent pass is:

    python3 scripts/test_pdf_truetype_bearings.py bin/test_pdf_pages bin/test_pdf_truetype bin/test_pdf_clip_oom --reference

Exact reference comparisons cover the aligned fill cases and component-origin
selection. Fractional transformed edges and text-clip boundaries retain vector
oracles; their scan conversion still differs from the comparison raster.

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

## Type 1 programs

MMType1 font dictionaries accept embedded ordinary Type 1 snapshots, as required
by ISO 32000-1 section 9.6.2.3. The renderer does not generate instances from a
multiple-master design space.

FontFile streams use Length1/Length2/Length3 after PDF stream filters have been
removed. The reader decrypts the binary eexec portion and the individual
charstrings, preserving their declared byte boundaries. In particular, a line
feed at the beginning of binary charstring data is not trimmed after a carriage
return delimiter. Private dictionaries with no eexec layer are accepted as a
compatibility form. The PDF reader does not load standalone PFB containers or
ASCII-hex eexec files.

The parser reads the standard font dictionary organization, FontMatrix,
StandardEncoding/custom encoding arrays, Subrs, CharStrings and lenIV. It
recognizes RD, -| and equivalent named readstring procedures. Redefinitions replace
the preceding Subrs or CharStrings dictionary. Glyph definitions are sorted and
deduplicated once, then names use binary lookup rather than a growing linear scan. The lenIV=-1 convention is accepted
without charstring decryption. The parser does not execute arbitrary PostScript.

Type 1 outlines support sidebearings and width vectors, line/cubic paths,
subroutines, division, seac accents, and standard OtherSubrs 0 through 3 for flex
and hint replacement. Stems and hint replacement are parsed but not grid-fitted.
Open strokes remain open; explicit closepath closes the path without resetting
the charstring's current point. Accents use StandardEncoding component names.
PDF encodings can override the program's vector, including MacExpertEncoding's
165 named entries from ISO 32000-1 Annex D.4.

Programs are limited to 16 MiB, 65,535 glyphs, 65,536 subroutines, 127-byte names,
24 charstring operands, ten nested subroutine calls and 65,536 execution steps
per glyph. Glyph and subroutine strings are bounded to 65,535 encoded bytes.
The decoded private section and lookup arrays count against retained font storage.
Custom OtherSubrs, dynamically generated PostScript font data and synthetic or
multiple-master programs remain unsupported.

## Decoded outline cache

TrueType, Type 1 and CFF programs keep a bounded cache of decoded, unhinted
font-space outlines. Each program has sixteen entries keyed by glyph ID. The
program's identity separates independent font dictionaries and subsets, even
when they use the same glyph numbers or font names.

The cache contains no text matrix, size, color, clipping, opacity or rasterized
pixels. Those are applied after lookup. TrueType path construction leaves the
cached points unchanged, so a second use at a different position or transform
cannot reuse already-transformed coordinates. Type 3 execution remains separate;
its state-dependent drawing commands are not stored as these outlines.

A page can retain up to 16 MiB of outline data through a cumulative cache budget.
Evicted data is freed, but eviction does not replenish the budget. Once the
budget is spent, new glyphs decode normally without being retained; existing
entries can still be reused. Cache limits do not select a different glyph or
change its advance. Font destruction frees every retained outline.

`make test-pdf-font-cache` checks hits, ownership, independent programs, eviction
and exhausted budgets. Complete-document allocation tests first render without
the cache, then require cached rendering and any successful allocation-failure
recovery to preserve those pixels. Existing transformed-text, CID, clipping,
pattern and transparency regressions exercise the same renderer.

## Bounds and ownership

Each retained font program is limited to 16 MiB, with 64 MiB of program storage
per collected font table. A CIDToGIDMap is at most 131,072 bytes. Glyph decoding
allows 65,536 points, 4,096 contours, 4,096 component calls and 16 nested component
levels. Stroked outlines share the path's 4,096-segment bound. Pixel intersection
work and text clipping use the page's shared budgets.

Programs, maps, component points, curve edges and stroke masks are released on
both success and failure. Invalid required font resources or exhausted font-table
storage fail the page rather than silently replacing every collected font with a
bitmap fallback. An invalid glyph encountered in a loaded program also fails the
page. Invalid optional ToUnicode maps are discarded without losing valid glyph
programs, widths or CID metrics.

## What remains

[Issue #50](https://github.com/cupidthecat/cupidimage/issues/50) tracks font and
text fidelity in the current PDF work. The main gaps are:

- TrueType instruction execution, grid fitting and antialiased font coverage.
- CFF2, synthetic fonts, multiple-master interpolation, custom Type 1 procedures and variable-font data.
- Unicode variation-sequence cmap format 14 and legacy AAT post format 4.
- Complete CJK font rendering beyond the supported encodings and embedded programs.
- The eight remaining standard-font face/glyph gaps and general substitution for other nonembedded fonts.

The applicability of newer font formats and tables must be checked against the
PDF version; this list is not a claim that every PDF version requires them.
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
152 Type 2 checks and 34 PDF rendering checks. The rendering tests cover cubic
geometry, CID fonts, encoding overrides, accents, FontMatrix, all text modes and
OpenType wrapping. Twenty-five optional independent comparisons use:

    python3 scripts/test_pdf_cff_render.py bin/test_pdf_pages --reference

The clip-only comparison uses a second rasterizer because the first applies a
different boundary rule. The cubic arch also has an analytic per-pixel test.
The optional font-fixture regeneration script uses FontTools; ordinary tests do
not require it. The PDF allocation-failure suite includes CFF font dictionaries, outlines and
composite cleanup.

`make test-pdf-standard-fonts` verifies all 14 metric/encoding resources
(7,756 data correspondences), rejects five unknown font names, checks 14
metadata/budget boundaries, and runs 56
positioning tests. Optional independent comparisons use:

    python3 scripts/test_pdf_standard_fonts.py bin/test_pdf_pages --reference

The standard-font text is invisible; an embedded geometric glyph marks the
resulting text position. Expected markers lie on the pixel grid so edge
rasterization rules do not obscure metric errors. These comparisons verify
advances, not substitute outlines. Another 186 tests compare the bundled fonts
with identical embedded programs across text modes, transforms, encoding
overrides and ToUnicode changes. All 3,210 bundled glyphs decode in regular tests.
An optional independent outline/advance comparison requires FontTools:

    python3 scripts/compare_pdf_standard_outlines.py bin/test_pdf_standard

This checks the actual Type 2 paths before rasterization. It does not treat
unhinted raster output as identical to a hinted font rasterizer.

`make test-pdf-type1` runs 40 structure checks, 66 charstring checks and 31
rendering checks. Twenty-one selected raster comparisons run with:

    python3 scripts/test_pdf_type1_render.py bin/test_pdf_pages --reference

The clear-private and 90-degree Type 1 FontMatrix fixtures have analytic tests
only because the independent font loader rejects them. A normal sheared matrix
and matrix translation do have exact raster comparisons. Missing PDF Widths
uses an explicit fallback test, not a reference-fidelity claim.

Six selected cached Type 1 programs decode all their glyphs. In a two-page check,
the fractions of different color channels fell from 7.865% to 3.337% and from
7.847% to 3.370%. Both pages still differ. The full 1,007-document manual review
remains deferred.

`make test-pdf-truetype` runs binary-table/glyph-name checks and 53 PDF rendering
checks. The 4,360 lower-level checks include the full pinned glyph-name dataset;
they are not thousands of independent font-rendering features. The authored
geometric font covers holes, quadratic curves, off-curve contour starts, affine
components and point attachment. Regular tests need only Python's standard
library. FontTools is needed only to regenerate that original test fixture.

The mapping suite adds 100 cmap cases and 801 post-table name/bounds checks.
Most of the post checks enumerate the standard name table, not separate rendering
features. Optional independent checks need FreeType and FontTools:

    python3 scripts/test_pdf_truetype_cmaps.py bin/test_pdf_truetype --reference
    python3 scripts/test_pdf_truetype_names.py bin/test_pdf_truetype --reference

These compare 39 cmap results and 774 name lookups. The legacy post comparison
uses FreeType because the other font parser does not accept version 2.5.

For 27 optional independent raster comparisons, run:

    python3 scripts/test_pdf_embedded_fonts.py bin/test_pdf_pages --reference

The selected comparison fixtures separate outline behavior from fractional-pixel
hinting differences. Curve fills also have analytic pixel expectations. One
clipping comparison uses a second rasterizer because the first uses a different
boundary rule. The format-8/10 and post-only fixtures also use that renderer
because the first does not select glyphs from those authored tables. No whole-page visual-parity claim follows from these checks.

The Adobe Glyph List is pinned under `third_party/adobe-glyph-list`, with its
copyright and BSD-3-Clause notice retained. [Third-party notices](../THIRD_PARTY_NOTICES.md)
must accompany binary distributions. The 59 predefined CMaps are pinned under
`third_party/adobe-cmaps`, with their original BSD notices and per-file hashes.
Base encoding tables follow ISO 32000-1
Annex D.2, including its duplicate-space, soft-hyphen and currency rules.

Two selected cached pages were also compared with independent rasters. Different
color-channel fractions fell from 0.135% to 0.119% and from 5.288% to 2.795%.
The second page exposed invalid ToUnicode metadata discarding valid outlines;
that recovery path now has regression coverage. Neither page is an exact match.

## Standards

- [Adobe Type 1 Font Format](https://adobe-type-tools.github.io/font-tech-notes/pdfs/T1_SPEC.pdf)
  describes Type 1 dictionaries, encryption, charstrings and standard subroutines.

- Adobe Technical Notes [5176: CFF](https://adobe-type-tools.github.io/font-tech-notes/pdfs/5176.CFF.pdf)
  and [5177: Type 2](https://adobe-type-tools.github.io/font-tech-notes/pdfs/5177.Type2.pdf)
  define the compact font structures, standard tables and charstring operators.

- [ISO 32000-1](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf),
  sections 9.3 through 9.7 and Annex D, defines PDF text state, fonts and encodings.
- The OpenType [post](https://learn.microsoft.com/en-us/typography/opentype/spec/post)
  specification and Apple's [post table](https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6post.html)
  define the standard name order and legacy offset representation.
- The OpenType [glyf](https://learn.microsoft.com/en-us/typography/opentype/spec/glyf),
  [loca](https://learn.microsoft.com/en-us/typography/opentype/spec/loca),
  [head](https://learn.microsoft.com/en-us/typography/opentype/spec/head) and
  [cmap](https://learn.microsoft.com/en-us/typography/opentype/spec/cmap) specifications
  define the embedded font structures used here.
- [RFC 7995](https://www.rfc-editor.org/info/rfc7995/) discusses PDF publication
  requirements for RFCs, including embedded fonts and Unicode preservation.
  It is an application profile, not a replacement for the complete PDF
  specifications. This renderer does not claim PDF/A or PDF/UA validation.


## Text-object compositing

TK defaults to true: overlapping glyphs within one BT/ET object knock out earlier
glyphs without increasing opacity. TK false allows them to composite separately.
Type 3 d0 programs are collected as individual glyph objects; d1 programs supply
only shape, with the caller's color, pattern, mask and opacity applied once.
Text state changes survive ET, and text clipping takes effect after the implicit
group is composited. Nested or unterminated text objects and mid-object TK changes
are rejected.

`make test-pdf-text-knockout` checks these rules, including TrueType and Type 3
overlap, indirect TK values, saved state, missing glyphs and mask application.
See [PDF transparency](pdf-transparency.md) for memory bounds and the independent
renderers' differing text-overlap behavior.


Content font names decode `#XX` escapes before lookup. Literal-string line endings
and escapes preserve the PDF character-code rules, and TJ validates each element
rather than skipping malformed values. See [PDF content syntax](pdf-content.md)
for the supported grammar, exact tests and parser bounds.

## Optional values and character widths

Null dictionary entries have the same effect as omitted entries, including
undefined references and generation mismatches. The font path applies this to
Encoding, BaseEncoding, Differences, FontDescriptor, font-program streams,
Widths, MissingWidth and ToUnicode. CID fonts also retain the specified defaults
for null DW, DW2, W, W2 and CIDToGIDMap entries. Encoding CMap WMode and UseCMap
entries treat null as absent. A null higher-priority program entry does not
hide a valid lower-priority embedded program. Required values still need their
specified types; a null element inside a Widths array is not a numeric width.
Widths are required for most simple fonts. The existing embedded-advance fallback
for missing non-Type3 widths remains damaged-input compatibility, not evidence
that a conforming producer may omit required widths.

Simple-font Widths elements and MissingWidth accept indirect numbers. Widths are
indexed by the original character code before Unicode extraction mapping. Null
page Resources continue inheritance, while an explicitly empty dictionary still
shadows the parent. Null unused font and graphics-state entries do not discard
other resources. Forms with null Resources retain the invoking resource scope.

`make test-pdf-font-defaults` compares these cases with omitted-entry or explicit
numeric equivalents using standard, TrueType and compact font programs. Complete
page loads also run under allocation-failure injection, with successful recovery
required to preserve every output pixel. The resource-budget and malformed-font
reference tests now require an error instead of accepting changed fallback text.

Type 3 character procedures retain full object-number/generation identities through
encoding lookup. Reused object numbers therefore select the active generation
rather than losing its generation during glyph selection. Null or undefined
character-procedure entries act as missing entries; they do not resurrect an older
procedure. FontMatrix and FontBBox arrays accept indirect numeric elements, and
null Type 3 Resources inherit the invoking scope. Width advances retain only the
FontMatrix-transformed horizontal component, even for rotated or sheared glyphs,
as required by ISO 32000-1 Table 112.

`make test-pdf-type3-objects` covers these rules with complete generated documents,
exact vector equivalents and allocation-failure checks.

The optional reference pass covers generation changes and identity/quarter-turn
matrices. Sheared edge coverage and translated reflections retain exact analytic
vector checks, but currently differ from the reference raster; they are not
counted as reference passes.

## TrueType subtable selection

For name-based simple-font encoding, the Microsoft Unicode `(3, 1)` subtable has
priority. If it is absent, the Macintosh Roman `(1, 0)` subtable is used. When
that selected table cannot map a glyph name, the font's `post` names are checked
before trying other character maps. This prevents an unrelated secondary map
from overriding the named glyph. Full-repertoire and Unicode-platform maps remain
available when the specified paths cannot supply a mapping.

Mac OS Roman lookup includes the extra names in ISO 32000-1 Table 115. In
particular, Adobe's `Omega` glyph name maps through U+2126 to code 189; the Greek
capital omega scalar U+03A9 retains the same compatibility mapping.

`make test-pdf-truetype-selection` uses conflicting tables to make the selection
order observable, including explicit missing-glyph entries, named glyph fallback
and all Table 115 additions. Direct and indirect Differences arrays can contain
indirect character codes and glyph names. The shared Differences parser also
serves Type 1, compact and Type 3 fonts; required array members cannot be null,
cyclic references, fractional codes or malformed numeric tokens.

The optional reference pass checks 90 of the 106 current cases exactly. Mac OS
Roman Euro follows Table 115 but differs from the reference result; those cases
remain analytic checks. Extension-table cases and malformed-input rejections
also remain separate from reference passes.

## Text surface work

An implicit knockout group is allocated when a text string first needs to paint,
not when BT merely resets text state. Empty text objects and outline-font text
in invisible or clipping-only mode do not allocate a color surface. Clipping
masks, text advances and Type 3 painting keep their existing rules.

Text groups initialize backdrop colors only at pixels reached by glyph shape.
Group completion visits the rectangle containing contributed shape, including
zero-opacity objects that can knock out earlier paint. Explicit background
colors still cover the whole surface. The compositing equations and precision
are unchanged; this avoids charging a full-page pixel pass for each short text
object. Surface memory and page work budgets remain enforced.

`make test-pdf-text-surfaces` checks thousands of empty/invisible text objects,
a 264-object text grid, mode changes, clipping and empty pattern colors. Fault
injection compares lazy surfaces with an eager-backdrop baseline. Separate bounds
checks cover empty surfaces, zero opacity, disjoint painted pixels and explicit
backgrounds. Existing nested-group, soft-mask, pattern and knockout tests remain
part of the aggregate suite.

## Fallback advance failures

For the damaged-input case where a simple embedded font omits both Widths and
MissingWidth, CFF and Type 1 advances come from the decoded charstring width.
Advance lookup shares the page's outline cache with painting and preserves the
composite glyph's own width. Type 1 width vectors are transformed by FontMatrix.
PDF-provided metrics still take precedence.

A decoding or allocation failure now fails the page instead of substituting a
zero advance and shifting later characters. A genuine zero or negative width
remains valid. Fallback advance requests count against the existing per-page
65,536 glyph-operation budget, including invisible text; this bounds charstring
work even when no glyph pixels are painted.

`make test-pdf-font-advances` compares fallback positioning with explicit PDF
metrics across all eight text modes, signed/zero widths, composite accents and
transformed Type 1 width vectors. Allocation tests compare cached and uncached
renders. A failed allocation must either fail page loading cleanly or recover
with identical pixels. This compatibility path does not make omitted required
font metrics conforming.

## Extended standard-font coverage

The 12 Latin faces use unmodified Liberation Fonts 2.1.5 programs for known AFM
glyphs absent from the compact outlines. A name in the font's post table takes
priority; Adobe glyph-name mapping and the Microsoft Unicode cmap provide the
fallback lookup. Unknown AFM names still select .notdef rather than accidentally
exposing an unrelated glyph from the larger substitute.

Each selected face is decompressed only when needed, owns its bytes, and is charged
to the existing 64 MiB font-program budget. Extended outlines have their own cache
entries but share the page's cache and glyph-work limits. All text modes, clipping,
transforms and source colors use the regular TrueType drawing path. Advances still
come from PDF Widths/MissingWidth or the standard AFM data, never from the extended
font's layout metrics. Existing covered glyphs keep their original outlines.

`scripts/test_pdf_extended_fonts.py` compares the 1,016 added glyphs with the same
unmodified programs embedded explicitly in PDFs. It also covers all text modes,
transforms and allocation failures. These are same-program equivalence checks,
not a claim of visual parity with other font substitutes. Font hinting and font
antialiasing are still unfinished.

Provenance, original notices and hashes are under
`third_party/extended-standard-fonts`. The original fonts and their generated data
remain under SIL Open Font License 1.1; see THIRD_PARTY_NOTICES.md.

The optional independent geometry pass checks all 1,016 added unhinted outlines
against FontTools, including contour endpoints and on/off-curve flags:

    python3 scripts/compare_pdf_extended_outlines.py bin/test_pdf_truetype

`scripts/generate_pdf_extended_fixtures.py` creates a 12-face visible sample and
an analytic advance-marker asset. The visible sample is decoded-unverified until
its raster is independently qualified; only the advance marker has an analytic
pixel reference.
