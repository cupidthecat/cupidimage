# Format conformance

Issue [#45](https://github.com/cupidthecat/cupidimage/issues/45) tracks the full
format review. This is a coverage record, not a claim that every feature of
these specifications is implemented. Unsupported features remain open even
when the current regression suite passes.

## Implemented work in this branch

PNG, TIFF Deflate strips, and PDF Flate streams share one RFC 1950/1951 decoder.
It checks the zlib header and Adler-32, rejects malformed Huffman trees and
length runs, accepts literal-only dynamic blocks, and decodes short final codes
without reading checksum bytes as compressed data. PDF/TIFF no longer retry a
failed zlib stream as raw DEFLATE. Preset dictionaries are not supported by
these container paths.

PNG checks chunk CRCs, header and palette placement, consecutive IDAT chunks,
transparency placement, unknown critical chunks, and IEND. Unknown ancillary
chunks remain skippable. Empty IDAT chunks are valid. The image allocation size
is checked before multiplication.

WebP validates RIFF lengths, chunk padding, duplicate image chunks, and canvas
sizes. Lossy alpha supports raw and lossless-compressed data, all four spatial
filters, and the informational preprocessing flag. VP8 keyframes use separate
header and coefficient readers, including 1/2/4/8 coefficient partitions, the
correct keyframe trees and update probabilities, and U-then-V coefficient
ordering. Intra prediction uses complete macroblock planes and the specified
upper-right boundary samples. The loop filter handles macroblock/subblock
edges, sharpness, mode/reference deltas, and simple versus normal filtering.

VP8 output uses nearest-neighbor chroma upsampling and integer limited-range
BT.601 conversion. It will not necessarily match a viewer's interpolated chroma
output pixel for pixel. The independent regression corpus compares against
FFmpeg-decoded YUV planes converted with the documented output policy. It covers
49 owned images, including varied dimensions, qualities, and partition counts.
This does not establish exhaustive VP8 conformance. VP8 decoding rejects output
over 64 megapixels. Animation and color-profile conversion remain unfinished.

PDF trailer lookup now ignores nested dictionaries and strings when locating
Root. This fixes the linearized empty PDF in the requested PDF.js corpus.
Synthetic cases cover misleading nested keys and a missing real root.

## Coverage and remaining work

The [PDF feature inventory](pdf-feature-matrix.md) records the expanded checklist
with per-item status and evidence. It is not a completed standards audit.

| Format/output | Existing coverage | Known gaps or remaining qualification |
| --- | --- | --- |
| ANSI truecolor | RGB escape output, alpha compositing, scaling, write failures | Cross-terminal rendering and the complete alpha/background option matrix |
| Kitty graphics | Transmission, chunking, compression, placement deletion, animation controls | Full protocol feature inventory and live-terminal qualification |
| Sixel | Palette generation, dithering, transparency, emitted stream checks | Device limits, raster aspect behavior, and live-terminal qualification |
| SVG | Static geometry, styles, filters, basic text, sampled animation | Full SVG/CSS semantics, fonts, external resources, and independent visual corpus |
| PNG | Static color types/depths, Adam7, transparency, framing, DEFLATE | APNG frame API, color management, and metadata exposure |
| JPEG | 8-bit sequential/progressive Huffman and arithmetic DCT, sampling through four, separate component scans | Lossless/higher-precision/hierarchical modes, color management and complete standalone qualification |
| WebP | VP8/VP8L still images, filtered/compressed alpha, container checks | Animation API/composition, ICC color conversion, broader codec differential tests |
| GIF | Animation, disposal, palettes, interlacing, transparency | Full extension/edge-case corpus and independent composition comparison |
| BMP | Existing Windows/OS2 and compression variants | Header/color-space variants and independent generated-format matrix |
| ICO/CUR | PNG/DIB entries, directory pages, masks, cursor hotspots | Full DIB depth/compression coverage and malformed directory combinations |
| TIFF | Classic/BigTIFF, many photometrics and compression methods | JPEG tiles, planar/tiled YCbCr, predictors/sample formats, complete tag matrix |
| TGA | Existing raw/RLE, origins, palettes, extension metadata | Full format/depth/attribute combinations and independent output comparison |
| PDF | Revision-aware objects, text positioning and unhinted outlines, paths, forms, images/masks, tiling patterns, seven shading types, RGB/gray groups and text knockout | Font fidelity/substitution, image interpolation, color management, further transparency spaces, annotations, optional content, JPX/JBIG2 and full-document qualification |
| HEIC/HEIF | Todo; no decoder implemented | Container, item properties, codec support, image sequences, color/HDR and metadata |

The 49 files in `assets/webp/reference/` contain real WebP streams generated
from owned synthetic images. Older PNG files mislabeled as WebP have been renamed
and moved to `assets/png/`. See [test assets](test-assets.md) for provenance.

The progressive JPEG audit found an AC-refinement ZRL bug: the decoder counted
sixteen spectral positions instead of sixteen zero coefficients. Existing nonzero
coefficients must consume refinement bits without shortening the zero run. The fix
also rejects runs and newly inserted coefficients that exceed the scan band.
Seventy-two owned grayscale and RGB 4:4:4 JPEGs now compare against independent
libjpeg pixels, including baseline/progressive scans, odd dimensions, noise,
gradients, and three quality levels.

## References

- [ITU-T T.81, Annex G: progressive JPEG](https://www.w3.org/Graphics/JPEG/itu-t81.pdf)
- [PNG Third Edition](https://www.w3.org/TR/png-3/)
- [RFC 1950: zlib](https://www.rfc-editor.org/rfc/rfc1950)
- [RFC 1951: DEFLATE](https://www.rfc-editor.org/rfc/rfc1951)
- [RFC 6386: VP8](https://www.rfc-editor.org/rfc/rfc6386), especially sections 8–15
- [RFC 9649: WebP](https://www.rfc-editor.org/rfc/rfc9649)
- [SVG specification](https://www.w3.org/TR/SVG/)
- [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/)

Additional format references and tests will be recorded as those reviews are
completed. The requested PDF corpora are
[py-pdf/sample-files](https://github.com/py-pdf/sample-files) and
[PDF.js test documents](https://github.com/mozilla/pdf.js/tree/master/test/pdfs).
Check provenance and licenses per fixture before redistribution; a repository's
code license alone is not evidence that every third-party document has that
license.

## Tests

Run the existing suite plus the new conformance tests:

```sh
make -j2 all test
make test-inflate test-png test-webp-container test-webp-pixels
```

The DEFLATE test uses Python's zlib as an independent oracle. The WebP pixel
fixture stores input hashes and expected decoded pixels, so routine tests do
not need Pillow, FFmpeg, downloaded images, or network access. The ordinary
GCC/Clang sanitizer jobs run these tests through `make test`.

The opt-in real PDF corpus uses four immutable, SHA-256-pinned documents
from the requested repositories. It checks all six pages and the blank page's
pixels. It is a load/structure smoke test, not a font or visual equivalence test.
All four selected documents are vendored with their license notices. The PDF.js
samples include font notices for embedded DejaVu subsets. See the asset guide.

```sh
make test-pdf-corpus
# Offline replay:
python3 scripts/test_pdf_corpus.py bin/test_pdf_pages
```

Regenerating the owned WebP corpus requires Pillow with WebP support and FFmpeg
with libvpx enabled:

```sh
python3 scripts/generate_webp_reference.py
```

Encoder version changes can change the compressed bytes. Regeneration is an
explicit maintenance step; normal tests never update their expected output.

## Corpus test system

`make test-asset-corpus` checks every committed image asset and exposed page/frame,
then validates bounded terminal-output samples. Independent pixel matches and
decoded-but-unverified images have separate report statuses. Invalid ICO reserved
headers are now rejected; the all-asset run exposed that acceptance bug.

Page-tree Type, catalog Pages, Kids, and Contents lookups now use the outer PDF
dictionary rather than nested/string decoys. Page content supports ASCIIHexDecode
and rejects unknown filters instead of interpreting compressed bytes as operators.
The cumulative decoded page content limit is 64 MiB.

The opt-in full PDF.js catalog contains 1,007 tracked PDFs and lists 460 external
link entries separately. See [testing](testing.md) for first-page versus all-page
audits, process limits, resume behavior, and independent raster comparisons.

ICO header validation follows [Microsoft's ICO format description](https://devblogs.microsoft.com/oldnewthing/20101018-00/?p=12513).

## PDF parsing fixes from the corpus

The next parser batch accepts an EOF marker ending at the final byte, treats
marked-content property dictionaries as bounded operands rather than hex strings,
and resolves direct/indirect stream lengths exactly. Indirect integer chains have
cycle and depth checks. Page Count is read from the outer dictionary rather than
nested decoys. A narrow compatibility repair accepts generation 65536 only on
the unused object-zero free-list sentinel; live entries and other generation
overflows remain errors. The normative free-list generation is still 65535.

These changes follow [ISO 32000-1](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf), especially object syntax, stream boundaries, cross-reference tables, and marked-content property lists. They do not implement optional-content visibility, complete fonts, or all image filters.

In a targeted all-page run on the same 225 cached corpus files, document errors
fell from 114 to 8 and rendered pages increased from 70 to 103. Additional image
failures became visible once those documents parsed. These are decoding results,
not a visual-conformance score, and do not substitute for a complete-corpus rerun.

## Image mask coverage

Image XObjects now support explicit one-bit masks and grayscale soft masks,
including independent mask dimensions, Decode polarity, SMask precedence, and
Matte recovery. The implementation follows ISO 32000-1 sections 8.9.6.3 and
11.6.5.3. Synthetic fixtures have analytic pixel expectations; an optional
independent raster comparison checks them with vector antialiasing disabled.
Malformed and nested masks are rejected. Mask allocation failures are covered by
the render allocation-injection test.

This does not add graphics-state soft masks, transparency groups, blend modes,
or image interpolation. A successful real-document decode is not a visual pass.

## Stream filter coverage

Page content, forms, and sampled images share bounded ASCIIHex, ASCII85,
RunLength, LZW, and Flate byte decoding. Filter arrays apply each stage in order.
Tests cover ASCII85 partial tuples and overflow, run-length literal and repeat
boundaries, missing terminators, invalid chains, and the decoded-size limit.
The rules come from ISO 32000-1 sections 7.4.2 through 7.4.5.
Direct DecodeParms dictionaries and aligned parameter arrays support Flate/LZW
prediction. LZW tests cover both EarlyChange values, all code widths, table
saturation, resets, and the next-code special case. Chained predictor tests cover
1/2/4/8/16-bit gray, RGB, and CMYK samples. CCITT fax decoding adds Group 3, mixed
Group 3, and Group 4 one-bit images, including bounded damaged-row recovery.
JPX, JBIG2, fax uncompressed extension modes, and indirect parameter objects in
general chains remain separate work.

## Graphics-state opacity and blend modes

ExtGState now applies `ca`, `CA`, `LW`, and `BM`, including all 16 standard RGB
blend modes. Regression cases check blend endpoints, state restoration, forms,
soft-mask multiplication, and stroke coverage. Combined fill/stroke operators
composite one object rather than painting the stroke over an already blended
fill, as required by ISO 32000-1 section 11.6.2. Blend functions follow section
11.3.5. Independent comparisons use a second rasterizer for combined fill/stroke,
where the installed primary rasterizer composites the two operations separately.
This remains device-RGB compositing, not full transparency-group support.

## Fax image validation

Fax decoding follows ISO 32000-1 section 7.4.6 and Table 11, with run codes
from ITU-T T.4/T.6. The decoder lives in `src/compression/ccitt.c`; TIFF and PDF
share the existing run-code tables without changing TIFF's strip decoder.
Thirty-four direct filter checks cover row counts, RTC/EOFB, mixed coding,
polarity, byte padding/alignment, malformed parameters, truncated runs and
recovery limits. Sixty independently encoded fixtures produce 180 exact PDF
full-resolution decoded-raster and rendered-image comparisons across both
polarities and applicable termination modes.
Allocation-failure tests cover initial buffers and output growth.

These tests establish the listed fax subset. They do not establish full PDF
rendering conformance or visual correctness for unrelated real-document features.

The pinned `ccitt_EndOfBlock_false.pdf` corpus document now decodes. All six
embedded fax images match an independent decoder at every visible source pixel,
covering all three `K` modes with both termination settings. The full rendered
page still differs in 1.585% of RGB channels from the comparison raster. It
remains a visual difference, not a corpus pixel pass.

## Inline image boundaries

`pdf_inline.c` parses inline dictionaries separately from ordinary content
operators, expands standard abbreviations and passes a borrowed image view to
the shared renderer. Raw, ASCIIHex, ASCII85, RunLength, Flate and JPEG boundaries
are parsed from the encoding rather than guessed from an `EI` byte sequence.
The DEFLATE prefix reader reports the checksum boundary; non-PDF whole-stream
callers still reject trailing bytes.

The inline suite has 134 checks covering packed/16-bit samples, predictors,
filter chains, stencils, palettes, named resources and cycles, transforms,
clipping, forms, embedded marker-like bytes, malformed syntax and continued
content after `EI`. Ten selected cases also pass an optional independent
raster comparison within one channel value. The DEFLATE suite adds 60 prefix
and trailer checks to its 307 existing cases. Direct LZW and fax inline images
still need encoding-aware boundary readers.

## Text positioning and simple-font advances

Text handling now follows the matrix and spacing rules in ISO 32000-1 sections
9.3 and 9.4. The implementation separates glyph geometry from text positioning,
uses declared simple-font widths before Unicode mapping, and implements all
eight text painting/clipping modes. Later sections cover Identity CID metrics
and embedded TrueType outlines; complete font fidelity remains open.

The 76-case text-state suite checks matrix composition, reflections, skew, line movement,
spacing, scaling, rise, graphics-state restoration, invisible advances, quote
operators, clipping union/intersection, font widths, source-code word spacing,
and malformed operators. The 97-case PDF allocation-failure suite includes stroked and clipped
glyphs. Existing font-map, form, graphics, transparency and asset tests remain
required; passing them does not establish real-font or full-document parity.

## Type 3 font programs and resource scope

Type 3 glyph execution follows ISO 32000-1 section 9.6.5 and Tables 112–113.
The authored suite has 48 checks for glyph programs, matrices, widths, Encoding,
local resources, nested calls, image masks, graphics-state/path isolation,
escaped names, malformed metrics and recursion bounds. The 111-case PDF
allocation-failure suite includes Type 3 metadata, streams and image resources.
Optional independent comparisons cover 28 cases: 27 exact raster comparisons
and one secondary-renderer comparison allowing a one-pixel glyph-cache boundary
shift. All analytic vector comparisons remain exact. The shared-stream test is
accepted under distinct resource contexts; one comparison implementation
instead rejects that re-entry.

The targeted seven-document corpus check is separate from those fixtures.
At the Type 3 checkpoint, four pages decoded with remaining pixel differences
and three failed on unsupported content or invalid glyph programs. The later
tiling-pattern work also renders the no-cycle nested-font sample, while the
cyclic example still fails safely. Successful loading
is not a visual pass, and the full 1,007-document user review remains deferred.

## Tiling/shading patterns and affine strokes

The pattern implementation follows ISO 32000-1 sections 8.7.2–8.7.4 and
11.6.7. It uses the permitted isolated-cell optimization only for Normal blending
inside the cell. Backdrop-dependent tiling cells now use per-object non-isolated
evaluation, as described in [PDF patterns](pdf-patterns.md).
Per-page cache ownership keeps saved graphics states valid and releases cells on
both success and error paths.

The pattern suite has 76 checks and 41 optional independent comparisons.
Gradient comparisons allow one or two channel units for reference approximation;
small analytic fixtures still require exact pixels.
One nested-pattern comparison uses a second renderer because the first shifts
that cached cell's phase by a pixel. Analytic expectations remain exact. Tests
cover coloured/uncoloured cells, spacing, matrices, parent-coordinate anchoring,
BBox clipping, masks, nested forms/fonts/patterns, opacity initialization,
recursion and cache limits. Shading tests cover function-based, axial and radial
patterns, Background/BBox interaction, and repeated resource-cache reuse.
The 155-case PDF allocation-failure suite includes
pattern descriptors, rasters, child states and resources.

The targeted three-document nested-Type-3 check now renders two pages and
rejects the cyclic example; both rendered pages still have reference pixel
differences. This is not a full-corpus or visual-parity result. Full user ratings
remain deferred while feature coverage grows.

## Stroke geometry and dash state

Stroke styling follows ISO 32000-1 sections 8.4.3.3–8.4.3.6. The new module
covers caps, joins, miter cutoff, odd/zero dash entries, phase, affine metrics,
subpath resets and graphics-state dictionaries. The 60-case suite includes
analytic circles/polygons and 26 exact optional independent comparisons. A second
renderer checks selected vertical endpoints where the first adds a boundary
pixel. Round/bevel boundary cases use exact analytic expectations rather than
claiming identical scan conversion across renderers.

The old degenerate-curve assertion assumed a round cap without selecting it.
It now checks that default butt caps leave the point unpainted, alongside an
explicit round-cap dot check. The PDF allocation-failure suite has 173 checks,
including dash parsing and fragment-buffer growth. Stroke adjustment and
antialiased font-outline fidelity remain open.

## Identity CID positioning

Identity-H/V code consumption and DW/W/DW2/W2 metrics follow ISO 32000-1
sections 9.7.4.3 and 9.7.5.2. The 62-case generated suite compares fallback glyphs
at analytically positioned origins, including indirect values, affine/reflected
matrices, negative font sizes, horizontal scaling, spacing, TJ adjustments and
all text painting/clipping modes. It does not compare embedded font outlines or
claim complete CID font rendering. The allocation-failure suite now has 198
checks, including malformed metrics, recursive references and partial tables.

## Embedded TrueType outlines

The font subsystem now separates binary tables, base encodings, glyph-name data,
PDF font resources and outline coverage. Supported programs use their glyphs,
not bitmap substitutes. The authored suite has 4,356 table/name checks, including
the pinned glyph-name dataset, and 45 rendered-page checks. Nineteen selected
raster comparisons are exact; analytic quadratic expectations are separate.
The PDF allocation-failure suite has 356 checks.

A focused two-document cached CID check renders both pages. Different-channel
fractions fell from 0.186% to 0.046% for one page and from 2.054% to 0.560% for the
other. Both still differ from the independent output. An obsolete composite flag
found during that check now has an authored regression. This was not the full
1,007-document review, which remains deferred.

[The font guide](pdf-fonts.md) records the missing hinting, charstring and encoding
work, authoritative specifications, and the limited role of RFC 7995.

## Encoding CMaps

Embedded maps support one-to-four-byte codespaces, CID and notdef ranges,
UseCMap inheritance, invalid-code recovery and original-source word spacing.
The focused suite has 68 checks and seven optional exact raster comparisons.
The bundled 59 standard CJK maps are checked against 885,128 source mappings,
including repeated/inherited entries, with six additional authored rendering
cases and exact independent comparisons. These are mapping checks, not proof
that every CJK glyph renders correctly. Missing font programs still need
faithful substitutes. The PDF allocation-failure suite has 414 checks.

The resources retain their BSD notices and pinned hashes. Full ordinary and
sanitizer validation must accompany changes to the decoder or generated data.

## CFF outline programs

The shared font-program adapter now loads embedded CFF/Type 2 programs as well
as TrueType. CFF structure tests cover name/CID charsets, encodings and private
font dictionaries. Execution tests cover cubic paths, widths, subroutines,
transient storage, arithmetic and accent composition. Rendering checks compare
explicit geometry and selected independent rasters; the cubic arch has analytic
pixel expectations. Hinting, CFF2 and non-CFF Type 1 programs remain open.

A focused cached-font check found a zero-stem counter mask with no mask bytes.
The reader now treats it as an empty mask, and the regression suite covers it.
Two selected cached CFF programs decode all their glyphs after that repair.
This is not a full-corpus rendering or visual review.

## Type 1 programs and empty text clips

Binary Type 1 font streams now use their decoded outlines instead of bitmap
fallback glyphs. The font parser and charstring interpreter are separate modules.
The tests cover encryption, readstring boundaries, encoding overrides, flex and
accent composition. MacExpertEncoding is included for Type 1 and CFF fonts.
Hinting, custom OtherSubrs and general PostScript font-program execution remain
open; the current implementation does not claim complete Type 1 fidelity.

A space-only clipping regression was confirmed against an independent renderer.
The text object now retains an empty glyph clip and intersects it at ET. Type 3
fonts retain their separate text-rendering-mode behavior.

## Standard PDF font advances

The 14 canonical nonembedded Type 1 fonts now use pinned Adobe AFM metrics,
including the built-in Symbol and ZapfDingbats encodings. Base encodings and
Differences select widths by glyph name. Explicit PDF widths take precedence;
ToUnicode does not change the one-byte source encoding or advance. This covers
spacing and bundled CFF substitute outlines. The 56 positioning fixtures, 186
substitute rendering checks and optional independent path comparisons are
described in [PDF fonts and text](pdf-fonts.md). Extended glyph coverage and
hinting remain incomplete; substitute fonts are not identical to every host's
font selection.

## Additional TrueType glyph selection

Font cmap formats 2, 8 and 10 now complement 0, 4, 6, 12 and 13. The post reader
supports the complete standard Macintosh name order in versions 1 and 2, custom
version-2 names and legacy version-2.5 signed offsets. This work adds 100 cmap
checks, 801 name/bounds checks and six PDF rendering cases. Optional independent
runs compare 39 mappings, 774 names and 25 embedded-font rasters overall.
Unicode variation-sequence cmap format 14 and AAT post format 4 remain open.

MMType1 dictionaries also accept embedded ordinary Type 1 snapshots. This adds
one exact rendering comparison, not multiple-master interpolation support.

## Graphics-state fonts

ExtGState Font arrays now select the referenced font program and text-space size.
They use the same scope and lifetime as other collected fonts, with separate
object-reference cache keys. The 31-case suite covers state restoration, nested
resources, indirect and compressed values, malformed arrays and escaped names;
19 optional comparisons match independently rendered pixels. The PDF
allocation-failure suite now has 1,330 checks with no leaks in its tracked
allocations. Other ExtGState and transparency-group features remain incomplete.

## Shared simple-font encoding resolution

Type 3 fonts now use the same named/base encoding tables and Differences resolver
as other name-keyed fonts. Missing-name overrides clear earlier mappings instead
of retaining a stale glyph. Font and embedded CFF stream subtypes resolve through
bounded indirect references. The Type 3 suite has 60 cases, with 40 optional
comparisons under its documented boundary rules; the corresponding TrueType,
Type 1 and CFF suites have 27, 21 and 25 independent raster comparisons.

## Isolated RGB transparency groups

Transparency Form XObjects with I true and K false now composite on a transparent
RGBA backdrop, then apply their caller's alpha, blend mode and mask once.
Later additions below cover non-isolated groups and gray blending. Details
and memory limits are in [PDF transparency](pdf-transparency.md).

## Non-isolated RGB groups and precision

Non-knockout groups now support I false, including its default when I is omitted.
Their colors blend against the accumulated backdrop; separately tracked group
alpha allows that initial contribution to be removed before the group is painted
into its parent. Both group types retain floating-point color and alpha through
nesting. Pattern/shading cache construction is kept outside that tracking.

The group suite has 81 cases and 65 optional independent comparisons, including
all 16 blend modes at two member opacities and subnormal nested alpha. The shared
transparency-memory cap is 256 MiB; the PDF allocation-failure suite has 2,208 checks.
CMYK/CIE/ICC blending remains open.


## Graphics-state soft masks and gray groups

Alpha/Luminosity masks now support captured transforms, BC, transfer functions,
q/Q lifetime, nesting and image-local mask precedence. DeviceGray is supported
as a group blending space alongside DeviceRGB. The focused suites have 56 mask
checks and 81 group checks, with 44 and 65 optional independent comparisons.
The allocation-failure suite has 2,208 checks. Three new analytic assets cover
alpha masks, luminosity masks and a gray mask with an inverse transfer function.

[PDF transparency](pdf-transparency.md) records the analytic-only differences,
shared 256 MiB memory bound and remaining limitations. CMYK/CIE/ICC blending, default-space remapping and full shape/text
transparency are still open. These changes do not establish complete PDF support.


## Isolated page compositing

Pages now render on a transparent group surface before compositing onto white.
This fixes initial non-Normal blending, which previously used white as the
object backdrop. The page's Group dictionary supports DeviceRGB/DeviceGray,
indirect attributes and complete color-space objects; its I flag cannot disable display isolation.
The final composite does not reuse the last object's mask, clip or opacity.
Empty pages also validate their group attributes.

The page suite has 93 cases and 84 independent comparisons. Gray conversion
checks use analytic expectations, as explained in the transparency guide.
Existing assets still pass unchanged. Two new page assets bring the manifest to
346 files, with 178 reference passes and 160 decoded-but-unverified entries.
Floating-point byte conversion corrects a one-ULP underflow at half-byte ties
without changing the analytic shading expectations.


## Knockout groups and shape tracking

RGB/gray groups and pages now honor K. Group results retain shape separately
from alpha, and AIS controls whether constants and masks modify shape or opacity.
Non-isolated children of knockout groups inherit the initial backdrop. Images and
pattern caches preserve the distinction between transparent paint and no shape.
Pattern-cache alpha is no longer quantized before it paints into the page.

The knockout suite has 73 analytic checks and 16 independent comparisons. The
[transparency guide](pdf-transparency.md) records disagreements rather than
relaxing the one-byte comparison tolerance. Two new analytic assets bring the
manifest to 348 files. Overprint and additional blending spaces still need work.


## Text knockout and glyph-object painting

TK now controls whole-text-object grouping, with true as the default. Later
glyphs replace earlier glyphs in overlap regions; TK false keeps per-glyph
compositing. Type 3 d0 programs are collected as single glyph objects, while d1
programs build only shape and receive caller paint once. This also fixes repeated
opacity and soft-mask application within an uncolored glyph.

There are 36 focused checks and 14 optional independent comparisons. The
transparency guide explains where the tested independent renderers disagree.
Two new analytic assets bring the manifest to 350 files, with 182 reference
passes, 160 decoded-but-unverified files and 1,026 terminal round trips.
The allocation-failure suite has 2,208 checks. Full font fidelity, color management
and the remaining PDF features are not established by these tests.


## Content text syntax and resource names

Content token parsing now has its own module. Names decode hexadecimal escapes
once, literal CR/CRLF normalizes to LF, and hex strings require a closing delimiter.
TJ rejects invalid element types, text operators check their operands/context,
and malformed or oversized numbers are not truncated into different values.
Operand overflow also returns an error instead of discarding data.

The parser suite has 821 checks, and 69 rendering checks include 36 exact
independent comparisons. Four newline comparisons use the secondary renderer;
see [PDF content syntax](pdf-content.md). The 352-asset manifest has 184 reference
passes and 160 decoded-but-unverified entries. Existing assets retain their
expectations. Two new assets cover font-name escaping and literal CRLF handling.


## Gouraud triangle meshes

Shading types 4 and 5 now decode their prescribed coordinate, component and flag
widths, byte padding, connectivity and Decode ranges. Vertex parameters are
interpolated before an optional function is evaluated; Indexed vertices convert
to their base space before interpolation. A complete shading paints once, rather
than repeatedly applying opacity at triangle overlaps.

The focused suite has 157 analytic checks and 110 optional independent comparisons.
Four new mesh assets bring the manifest to 356 files, with 188 reference passes
and 160 decoded-but-unverified entries. The allocation-failure suite has 2,208
checks, including mesh memory/work bounds. Gradient sampling still differs from
the tested independent renderers; the [shading guide](pdf-shading.md) records those
limits and the pinned corpus fixture that fails its declared stream length.

The same specification review corrected color-space handling: single-element
device-space arrays are valid, but arbitrary ColorSpace resource names are not
valid color-space objects in shading/group dictionaries or resource values.
Three former positive alias cases now reject those invalid definitions. Content
operators can still select a resource that directly defines a supported space.
Color management and other unfinished PDF features remain open.


## Coons and tensor-product patch meshes

Types 6 and 7 now decode full/shared-edge patch records. Coons boundaries convert
to bicubic control nets; tensor interior controls remain independent. Adaptive
subdivision bounds numerical inverse solves at pixel centers. Per-pixel parameter
priority selects the specified result when a patch folds over itself, and later
patches replace earlier ones. Corner colors and optional function inputs are
interpolated in parameter space.

The patch suite has 219 checks and 124 optional independent comparisons, including
40 analytic bisection comparisons. Four new assets bring the manifest to 360 files,
with 192 reference passes, 160 decoded-but-unverified entries and 1,056 terminal
round trips. The allocation-failure suite has 2,208 checks. The shading guide
records subdivision/work bounds and the remaining pixel differences.

The pinned Coons/function corpus document now renders without modification after
adding the valid single-element Pattern color-space form. The focused tensor
source page still needs CMYK page groups and DeviceN support; its isolated tensor
objects rendering does not establish whole-page support. All seven shading
geometries are represented, but full PDF coverage and visual parity remain open.


## Native colors and backdrop-dependent tiling

Source device components now remain in double precision through fills, strokes,
text, images and all seven shading geometries. DeviceCMYK converts directly to
gray under section 10.3; it no longer loses ink contributions through an
intermediate clipped RGB value. The device-color suite has 74 analytic cases.

Non-Normal tiling cells retain their definition and render all visible tiles in
one non-isolated group for each painted object. Caller opacity, masks and clipping
apply once to the extracted source. The new pattern-backdrop suite has 43 analytic
cases, and the allocation-failure suite has 2,476 checks. The 360-asset manifest
and its 192 reference passes, 160 decoded-unverified entries and 1,056 terminal
round trips are unchanged.

The independent renderers tested for the new Multiply cases disagree with the
specification's non-isolated result. Those cases remain analytic checks, not
reference passes. Additional color spaces and CMYK blending groups still need work.
The full manual document-rating pass remains deferred.


## Destination-aware pattern colors

Ordinary colored tiling and shading caches now retain double-precision colors.
They keep their definitions and rebuild when reused in a different supported
blending space, preserving direct CMYK-to-gray conversion and avoiding early
byte rounding. Shading-cache reuse also distinguishes the initial AIS setting.

The pattern-color suite has 30 analytic cases, including compressed definitions,
RGB/gray/RGB transitions and sub-byte opacity. The allocation-failure suite has
2,526 checks after adding retained-copy and cache-rebuild cases. The existing
asset manifest and reference tolerances are unchanged. CMYK, calibrated and ICC
blending groups remain unimplemented.


## Shading-pattern state and background shape

Shading patterns now apply their own opacity, blend mode, masks and alpha-as-shape
inside the specified implicit knockout group. All seven shading geometries keep
background and main shading members separate, including meshes. Constant mesh
components use difference-form interpolation to avoid roundoff before blending.
Stroke and text parameters are ignored in a shading pattern's ExtGState.

Graphics-state parsing also resolves indirect and escaped blend-mode names,
indirect array elements and explicit null entries. The new shading-pattern suite
has 96 analytic cases; the general transparency suite has 177 cases and the
allocation-failure suite has 2,586 checks. Reference discrepancies are documented
in the pattern guide without widening tolerances. The 360 existing assets remain
passing, and the full manual document review is still deferred.

## Revision-aware PDF object resolution

Complete documents now use their cross-reference tables or streams rather than
selecting the first physical object with a matching number. The index preserves
revision precedence, active generations, free/null entries and compressed-object
indices, including hybrid files. Stream filter metadata and page content arrays
resolve indirect objects through that index. Legacy indirect cross-reference
lengths are verified against their own revision, not a newer replacement.

[PDF objects and revisions](pdf-objects.md) records the validation rules, limits
and focused fault-injection tests. This removes a loading prerequisite for font
and rendering work; it does not establish complete PDF support. Optional-value
consumers still need a broader null/default audit, and encryption and damaged-file
reconstruction remain unsupported.

The subsequent font audit applies null/default semantics to simple-font encoding
and metrics, embedded-program selection, optional ToUnicode maps, and inherited
page/form resources. Required font parsing and storage failures now fail the page
instead of clearing the entire font table and reporting a successful fallback
render. Optional extraction-map failures remain isolated from glyph programs.
Focused equivalence and complete-loader allocation tests cover these paths; the
broader audit of other optional rendering parameters remains open.

## Native PDF DCT image samples

The image filter pipeline now keeps JPEG components in their native color space
until PDF Decode arrays, masks and color conversion are applied. It supports raw
RGB, CMYK/YCCK, marker precedence, baseline/progressive images, indirect filter
controls and byte-filter chains ending in DCT. The duplicate image-only Flate and
predictor implementation has been removed.

Authored native-sample tests and raw-image equivalents verify decoding separately
from scaled-image raster behavior. Exact independent probes use native-resolution
pages; CMYK comparisons do not conflate unmanaged and color-managed output. See
[PDF images](pdf-images.md) for limits and test commands.

DCT support is still incomplete: other applicable coding modes remain under audit in
[#49](https://github.com/cupidthecat/cupidimage/issues/49). The wider PDF work in
#45 remains open.

JPEG sampling now accepts factors through four and checks the per-scan MCU
data-unit limit. Single-component scans use their own block grid. Components
retain their selected quantizers through output, allowing deferred definitions
and selector reuse between completed component scans. Focused sampling tests
cover edge blocks, restart wrapping, native samples and complete PDF images.

Sequential JPEG frames now support separate component scans and mixed component
groups. The decoder tracks component coverage, rejects duplicate or incomplete
sequential data, and shares coefficient decoding with the progressive path.
Interleaved single-scan images keep their streaming path; images needing several
scans are reconstructed after all components arrive.


Arithmetic-coded sequential and progressive DCT streams now share the native PDF
image path. Focused tests compare 60 independently encoded pairs, including
restart intervals, odd image dimensions and subsampled color components. The
8-bit extended sequential Huffman frame marker is also supported. This closes
another image-decoding gap, but does not establish complete DCT or PDF coverage.


Image and mask metadata now uses the shared indirect-value resolver. Optional
null entries retain defaults, including a null SMask falling back to Mask.
Decode, color-key and Matte arrays accept indirect numeric members. Shared
dictionary lookup preserves both the document index and shading stream bodies.
The former undefined-soft-mask rejection fixture now checks the required null
semantics instead. The remaining optional-value audit is still tracked in #48.
