# Format support

These are implemented subsets, not claims of complete standards conformance.
See [the conformance tracker](format-conformance.md) for remaining work and tests.

## SVG

static subset with shapes (rect/circle/ellipse/line/polyline/polygon/path including arc
commands), embedded images (`<image>` with `data:` URIs and local file hrefs), basic text
(`<text>/<tspan>/<textPath>` via built-in bitmap font; includes
`dominant-baseline`/`alignment-baseline` handling and UTF-8 bullet `•` fallback glyph),
presentation + inline styles + basic `<style>` selectors (descendant, child `>`, adjacent
`+`, and general sibling `~` combinators, plus basic `[attr]`/`[attr=value]` matching and
`display`/`visibility`), transforms, viewBox/preserveAspectRatio, linear/radial gradients,
patterns, `hsl()/hsla()` colors, stroke caps/joins/dash, clipPath, luminance and alpha masks
(`mask-type`), basic `<use>` references for primitive shapes and `<symbol>` (with symbol
`preserveAspectRatio` handling and wrapper style attributes), basic SVG markers
(`marker-start`/`marker-mid`/`marker-end`, including `orient=\"auto-start-reverse\"` and
improved join-angle placement), and filter primitives: `feGaussianBlur`, `feOffset`,
`feColorMatrix`, `feComponentTransfer`, `feMorphology`, `feConvolveMatrix`, `feTurbulence`,
`feDisplacementMap`, `feDiffuseLighting`, `feSpecularLighting`, `feTile`, `feImage`
(including `data:` URIs), `feFlood`, `feBlend`, `feComposite`, `feMerge` (including
Source/Background/FillPaint/StrokePaint filter inputs and primitive subregions). Basic
sampled animation support for SMIL (`<set>`, `<animate>`, `<animateTransform>`) and CSS
keyframes (subset: `transform`, `stroke-dashoffset`, `stop-color`) via
`cupidimage_svg_options.animation_time` / CLI `--svg-time`. Supersampled rasterization for
terminal preview. No scripting/DOM, no external web fetches, no custom font loading.

## PNG

non-interlaced and Adam7; color types 0/2/3/4/6 with 1/2/4/8/16-bit depths; palette + tRNS.

## JPEG

8-bit Huffman-coded baseline, extended sequential and progressive DCT, plus
arithmetic-coded sequential and progressive DCT; grayscale/YCbCr/CMYK/YCCK.
Sampling factors run from one through four, with separate and mixed sequential
component scans. Lossless, higher-precision, hierarchical and color-managed
output still need work. The recent shared-decoder changes support PDF DCT images;
they do not complete the standalone JPEG audit.

## WebP

VP8 lossy keyframes with 1/2/4/8 coefficient partitions; VP8X still images; VP8L lossless
with transforms/color cache; raw and lossless-compressed ALPH with all spatial filters. No
animation or ICC color conversion.

## GIF

single-frame decode and animated GIF composition; palettes, interlacing, transparency;
disposal methods 0-3; loop counts and per-frame delays.

## BMP

Windows/OS/2; BI_RGB/BI_RLE8/BI_RLE4/BI_BITFIELDS/BI_JPEG/BI_PNG; 1/4/8/16/24/32-bit;
top-down/bottom-up; pre-multiplied alpha conversion.

## ICO/CUR

directory enumeration + page-based decode; PNG-compressed and DIB icons at 1/4/8/24/32-bit;
AND mask transparency; CUR hotspots.

## TIFF

Classic + BigTIFF; byte orders II/MM; compression none/LZW/PackBits/Deflate/JPEG/CCITT
RLE/G3/G4; photometric bilevel/grayscale(+alpha)/RGB/RGBA/palette/CMYK/YCbCr/Lab;
strips/tiles; planar 1/2; predictor=2; orientation 1-8; multi-page IFDs (SubIFD pages via
page APIs).

## TGA

types 0-3/9-11; uncompressed/RLE; 8/15/16/24/32-bit; all origins;
color-mapped/RGB/grayscale; TGA 2.0 metadata + thumbnail + gamma/color correction.

## PDF

PDF pages support text positioning and clipping, unhinted TrueType/CFF/Type 1
outlines, Type 3 glyph programs, standard-font substitutes and horizontal/vertical
CID metrics. `--pdf-text-aa` enables grayscale outline fills; stroke and clip
coverage remain binary. `--pdf-tt-hinting` separately enables the native legacy
TrueType profile. Paths, styled strokes, forms, tiling patterns and all seven shading
geometries share the graphics pipeline. RGB/gray transparency groups, text
knockout and graphics-state soft masks are implemented within the limits below.

Images include packed and 16-bit device-color samples, indexed palettes, fax and
DCT streams, filter chains, Decode arrays, explicit masks and soft masks. Image
placement uses the current affine transform. Pages honor inherited MediaBox and
CropBox rectangles, nonzero origins and quarter-turn rotation.

Complete font fidelity, image interpolation, color management, additional
transparency spaces, annotations, optional-content visibility and several image
filters remain unfinished. The [PDF font](pdf-fonts.md), [image](pdf-images.md),
[shading](pdf-shading.md), [pattern](pdf-patterns.md), [object](pdf-objects.md) and
[transparency](pdf-transparency.md) guides describe the implemented paths and gaps.

## PDF details and limits

PDF pages render at 96 DPI, rounding fractional pixel extents upward. A page's
UserUnit scales default user-space units before text, images, forms and patterns
are placed. It defaults to one and is not inherited from the page tree. The
supported range is positive values through 75,000, subject to representable
transforms and the preview-size bound. Null optional page boxes and Rotate entries
continue inheritance; box array members and UserUnit may be indirect. A required
MediaBox must be found on the page or an ancestor, rather than guessed from a
paper-size default.

CropBox is intersected with MediaBox, and page rotation applies to both text and graphics. Direct and indirect page boxes and
rotation values are supported. Invalid boxes, cyclic parent chains, and previews
larger than 64 megapixels are rejected. Whitespace is preserved, so sparse content cannot change the
page dimensions or shift its origin.

Run the generated PDF page regressions with Python 3:

    make test-pdf-pages

PDF image XObjects use the page's Resources dictionary, including inherited and
indirect resource dictionaries. Images use nearest-neighbor sampling. Flate
images accept predictor 1 (none), 2 (TIFF), and 10-15 (PNG), with 1/2/4/8/16-bit samples.
Prediction follows each filter stage's declared row layout and defaults.
The decoded sample data is limited to 64 MiB per image.

DCTDecode accepts standalone names and filter chains. Native JPEG samples preserve
ColorTransform 0/1, Adobe marker precedence and PDF Decode arrays without losing
CMYK channels. Frame dimensions, bit depth and component count must match the
image dictionary. Flate/LZW stages accept indirect predictor dictionaries and
packed/16-bit prediction; each filter reads only its own parameters. The current
DCT path supports 8-bit Huffman baseline/extended sequential/progressive frames
and arithmetic sequential/progressive frames. Sampling factors run from one
through four, with separate or mixed sequential component scans. Remaining DCT
applicability questions, JPX and JBIG2 remain open. See [PDF images](pdf-images.md) for the pipeline,
checks and limits.
Drawing an invalid or unsupported XObject returns a load error.

Type 1 Form XObjects accept raw streams and the same byte filters as page content. Their Matrix transforms
the content, and BBox clips text, paths, and images, including nested forms.
Forms use their own Resources dictionary when present, or the caller's resources
otherwise. Drawing a form preserves the caller's graphics state and current path.
Built-in text glyphs follow the text matrix and current transformation matrix.
Supported embedded TrueType, CFF and Type 1 programs use their outlines. See [PDF fonts and text](pdf-fonts.md) for glyph selection and remaining font limitations.

Forms are limited to 16 nesting levels, 4096 combined form/shading invocations, and 64 MiB of combined
decoded form content per page, counting repeated invocations. Recursive and
external-reference forms are rejected. Isolated and non-isolated, RGB/gray transparency
groups are supported; other group configurations remain unsupported. See
[PDF transparency](pdf-transparency.md) for compositing and memory limits.

Run the generated form checks with Python 3:

    make test-pdf-forms

Path clipping supports `W` (nonzero winding) and `W*` (even-odd). The clip takes
effect after the path's painting operator, including `n`, and intersects the
current clip. It applies to paths, text, images, and nested forms. `q` and `Q`
save and restore the clip. Path coordinates retain their placement when the
transformation matrix changes. Clipping samples pixel centers without antialiasing.

Each page allows 4096 clip operations, 4096 segments per clipping path, 32 saved
graphics states, and 64 MiB of live clipping masks. A shared budget limits clipping
to 268,435,456 pixel-edge tests per page. Documents exceeding these limits return
a load error.

Run the clipping and allocation-failure checks with Python 3 and a C compiler:

    make test-pdf-clipping test-pdf-clip-oom

PDF colors support DeviceGray, DeviceRGB, and DeviceCMYK through `cs`/`CS` and
`sc`/`SC`/`scn`/`SCN`, as well as `g`/`G`, `rg`/`RG`, and `k`/`K`. Fill and
stroke color spaces are independent and follow `q`/`Q` state restoration.
Selecting a color space resets its color to black.

Named ColorSpace resources support device spaces and Indexed palettes for
vector fills and strokes, including inherited and
indirect dictionaries and indirect name values. Forms use their own resources
when present and otherwise inherit the caller's resources. Indirect object resolution is
bounded to reject cycles. Resource entries define complete color-space objects; chained resource aliases
are rejected. Device families and the colored Pattern space also accept single-element arrays. Pattern spaces are described below. Unsupported spaces,
including calibrated colors and ICC profiles, return a load error. Invalid color
operands also return an error. CMYK uses the built-in RGB conversion without
color profiles.

Run the generated color and resource checks with:

    make test-pdf-colors

Run the generated image checks with Python 3:

    make test-pdf-images


Page and form content accept FlateDecode (`Fl`), ASCIIHexDecode (`AHx`),
ASCII85Decode (`A85`), RunLengthDecode (`RL`), and LZWDecode (`LZW`). Filter arrays decode in order,
with at most eight stages and 64 MiB of output per stage. Unknown filters,
truncated encodings, and invalid ASCII85 tuples are rejected. DecodeParms may be
null, a direct dictionary for one filter, or an array aligned with the filter
array. Direct Flate/LZW dictionaries accept Predictor, Colors, Columns,
BitsPerComponent, and EarlyChange. Indirect DecodeParms objects remain supported
only by the single-Flate image path. Combined page content is limited to 64 MiB.

## Packed image samples and masks

Raw and Flate image streams support 1-, 2-, 4-, 8-, and 16-bit components, including
odd-width row padding. TIFF horizontal differencing operates on whole components;
PNG predictors operate on the correct packed byte stride. Sixteen-bit samples
retain their precision through prediction and Decode mapping before conversion to
8-bit RGBA.

Stencil ImageMask images use the current fill color, with normal or inverted
Decode polarity. Color-key Mask arrays make matching source samples transparent.
Explicit Mask image streams support one-bit stencils. DeviceGray SMask images
support 1/2/4/8/16-bit samples, Decode arrays, raw/Flate streams, and supported
predictors. Masks can have different dimensions from the parent image and are
sampled independently over its unit square. Image sampling remains nearest-neighbor;
Interpolate is not implemented.

SMask takes precedence over Mask. A Matte array removes preblending in the parent
color space before color conversion. Matte masks must have the same dimensions as
the parent. Nested masks, malformed masks, and unsupported mask codecs return an
error rather than drawing an opaque substitute. Graphics-state Alpha/Luminosity masks are also supported for the group spaces
described in [PDF transparency](pdf-transparency.md). Image-local masks override
the graphics-state mask for that image only.

Run `make test-pdf-masks` for exact-pixel and malformed-input coverage. The optional
`python3 scripts/test_pdf_masks.py bin/test_pdf_pages --reference` also compares
successful fixtures against an installed independent rasterizer, with vector
antialiasing disabled to match pixel-center clipping.

DeviceCMYK images, including DCT-compressed CMYK and YCCK, use the same unmanaged
conversion as device-color paint. Color-managed conversion remains unfinished.

## Deferred formats

HEIC/HEIF is planned for later. It is not implemented or included in supported-format claims.

## Indexed PDF images

Image XObjects accept Indexed color spaces with DeviceGray, DeviceRGB, or
DeviceCMYK palettes. Color-space arrays and palette strings may be indirect;
palettes can also be filtered streams. Literal and hexadecimal palette strings
follow PDF escaping and whitespace rules. Palette lengths and index bounds are
validated. Decode mappings round indices and clamp them to the palette range.
Color-key masks, explicit masks, and soft masks work with indexed images.
Indexed images with Matte preblending remain unsupported and return an error.

`make test-pdf-indexed` checks packed indices, palette sources, Decode mappings,
masks, and malformed or cyclic references. Full 16-bit indices and tolerated
indirect-reference chains use analytic expectations because the installed
comparison renderers do not preserve those inputs. DeviceCMYK expectations use
the library's unmanaged conversion.

## PDF opacity and blending

ExtGState dictionaries support font/size selection (`Font`), nonstroking opacity
(`ca`), stroking opacity (`CA`),
line width (`LW`), and all 16 standard RGB blend modes. Blend-mode arrays select
the first recognized mode, falling back to Normal if none is recognized. State
is preserved by `q`/`Q` and inherited by forms without leaking changes back to
the caller. Image soft-mask values multiply the nonstroking opacity.

Overlapping segments within one stroke are composited once. Combined fill/stroke
operators also form one object, with stroke coverage taking precedence over fill
coverage. This avoids dark seams caused by repeatedly blending the same object.
The tests compare analytic pixels and independent raster output, including blend
endpoint cases, state restoration, and mask/opacity combinations.

Pages render as isolated transparency groups and composite onto white only
after their content finishes. Their Group dictionary selects DeviceRGB or
DeviceGray; RGB is the default. Form groups also support these spaces. Knockout and non-knockout groups and graphics-state Alpha/Luminosity masks are
supported. CMYK/CIE/ICC group spaces and overprinting remain incomplete. Text objects
honor TK, with true as the default. See [PDF transparency](pdf-transparency.md)
for mask precedence and group limits. Stroke styling is described below.

Content names, text strings and TJ arrays follow the rules and resource bounds
in [PDF content syntax](pdf-content.md). Encoded resource names are decoded once;
text operand errors return a failure rather than silently dropping text.

## PDF character maps

ToUnicode streams use the shared byte-filter pipeline, including LZW and filter
chains. Font resources follow page inheritance and nearest-resource replacement;
inline font dictionaries and indirect resource/font dictionaries are supported.
Nested dictionaries, comments, and strings do not override outer Resources,
Font, ToUnicode, or Filter entries.

These maps choose characters for the existing bitmap fallback. Embedded font
glyph selection follows the font encoding instead; complete PDF CMap semantics
remain open. Simple-font Widths and MissingWidth entries control advances; fonts
without supported metrics retain the approximate fallback advances. Invalid
required outline metadata fails the page instead of silently selecting a fallback.
An invalid ToUnicode map alone does not discard valid outlines, Type 3 programs
or CID metrics. `make test-pdf-font-maps` checks those boundaries.

Character maps are limited to 65,536 entries per font and 256 fonts per resource
scope. Failures in optional ToUnicode maps discard only those maps. Required
font metadata and exhausted font-table storage fail the page rather than
attempting unbounded expansion or replacing all fonts. Entries are sorted once, duplicate codes keep
the last definition, and glyph lookup uses binary search.

## PDF object syntax

Object headers accept PDF whitespace and comments between tokens, and an object
body may begin directly after the `obj` keyword when separated by a delimiter.
Object scanning skips comments, literal strings, and hexadecimal strings rather
than treating embedded `obj`, `stream`, or `endobj` text as structure.
Object streams use the shared byte filters. Their object counts, header extent,
body offsets, and ordering are checked before extracting an object.

Complete PDF files now use a revision-aware cross-reference index, preserving
generations and free entries and selecting compressed objects by their declared
container and index. See [PDF objects and revisions](pdf-objects.md) for hybrid
precedence, indirect stream parameters, validation and limits. Encryption and
damaged-file reconstruction remain unsupported.

## PDF shadings

The `sh` operator paints function-based, axial, radial, free-form/lattice triangle
and Coons/tensor patch shadings. Supported color spaces include DeviceGray,
DeviceRGB, DeviceCMYK and Indexed, with the function restrictions described in
[PDF shading](pdf-shading.md). Supported color functions include one- or two-input Type 0 sampled functions, single-input Type 2
exponential functions, Type 3 stitching functions, Type 4 calculator functions,
and arrays of single-output
functions. Exponential functions accept Domain, C0/C1, N, and optional Range.
Stitching functions accept nested Functions, Bounds, Encode, Domain, and Range. The
renderer applies shading Domain and Extend values, BBox, the current transform,
path/form clipping, opacity, and blend mode. Numeric arrays and exponents may
be indirect objects. Gray functions can use the default C0/C1 values.

Each page has a shared budget of 128 million color-function evaluations, including
nested forms and stitched/component functions. Function trees are limited to
64 nodes, 32 pieces per stitch, and 16 levels of reference/structure nesting. Unknown shading or function types return an error rather than
silently leaving a blank area. Cubic sampled interpolation and color-managed
shading spaces remain unsupported.

Small fixtures require exact analytic pixels. Optional independent comparisons
use native-resolution pages, with one channel unit allowed for linear axial
shading and two for nonlinear reference approximation. A second rasterizer checks
radial gradients extending beyond the page, where the primary comparison tool
leaves a boundary-color rim. No resizing or alignment is used to obtain a match.

Stitching honors half-open subdomains, reversed Encode mappings, output Range
clipping, and the defined zero-width final subdomain. A single-piece stitch may
have a zero-width Domain. Function arrays and stitch children may use inline or
indirect dictionaries; cyclic references and over-budget trees return errors.

Sampled functions support 1/2/4/8/12/16/24/32-bit packed samples, linear interpolation,
Size, Encode, Decode, and Range. The first input dimension varies fastest. Sample
tables share a 64 MiB decoded-data limit per function tree and are released on
both success and failure. Low-order bits remain available to Decode mappings,
including mappings that magnify the lowest bits of a 32-bit sample. Cubic Order 3
interpolation is rejected rather than treated as linear.

Type 1 shading applies its Matrix and two-dimensional Domain before evaluating
the color function. The `sh` operator does not paint the shading's Background
entry, which applies only to shading patterns. Sample lookups are charged against
the shared function-work budget along with function-node evaluations.

Calculator streams support all 42 arithmetic, comparison, Boolean, bitwise,
stack, and conditional operators listed in ISO 32000-1 section 7.10.5 and Annex C.
Programs are compiled once into forward branches, with 4,096 instructions,
256 stack entries, and 16 syntax levels per function. Each executed instruction
and copied or rotated stack entry consumes the shared work budget. Invalid
syntax, type errors, non-finite results, and stack errors reject the rendering.
Calculator streams use the shared byte filters and decoded-data limit.

Indexed vector colors use the same palette parser as images. `cs` and `CS`
select palette entry zero initially; `sc`, `scn`, `SC`, and `SCN` round and clamp
the requested index. Fill and stroke palettes are independent and survive
graphics-state save/restore and form calls. Device-color operators replace
the selected palette with their device space.

## PDF fax images

`CCITTFaxDecode` (`CCF`) decodes one-bit Group 3 one-dimensional, mixed
one-/two-dimensional, and Group 4 data. DecodeParms supports `K`, `Columns`,
`Rows`, `EndOfLine`, `EncodedByteAlign`, `EndOfBlock`, `BlackIs1`, and
`DamagedRowsBeforeError`. End-of-block mode uses RTC/EOFB markers and takes
precedence over the row count. Decoded rows are byte-padded.

Damaged-row recovery is enabled only for nonnegative `K` with `EndOfLine true`.
It searches for the next line boundary and copies the preceding good row;
consecutive damaged rows become white. Recovery shares the decoder's work
budget and stops when the declared allowance is exhausted. Truncated codes,
out-of-range runs, missing required markers, and unsupported uncompressed
extension modes return errors. Width is limited to 1,048,576 pixels and decoded
output to 64 MiB.

## PDF inline images

Content streams accept `BI`/`ID`/`EI` images with full dictionary keys or the
standard inline abbreviations. Supported data boundaries are raw sample length,
ASCIIHex, ASCII85, RunLength, Flate, and JPEG. A recognized outer filter can
wrap other supported byte filters, such as ASCIIHex around LZW or fax data.
Direct LZW and fax inline boundaries remain unsupported and return a load error.

Raw samples use their dimensions and bit depth to locate the end. Flate uses
its final block and checksum; JPEG uses segment lengths and entropy escapes.
Text resembling `EI`, `endstream`, or `endobj` inside pixels or compressed
payloads does not terminate the image. Missing or unmatched delimiters fail.

Inline images reuse XObject sample decoding, palettes, stencils, clipping,
transforms and compositing. Device and named resource colour spaces are
supported, as are Indexed palettes and Decode arrays. Dictionary parsing is
limited to 64 KiB, 128 entries and 16 nested containers; image data is bounded
at 64 MiB. Unknown inline keys are ignored as required by ISO 32000-1 section
8.9.7. This does not add image interpolation or the remaining colour spaces.

## PDF text state and positioning

Text uses the complete six-component text and line matrices. `Tm`, `Td`, `TD`,
`T*`, `Tj`, `TJ`, and both quote operators apply text-space positioning before
the current transformation matrix. `Tc`, `Tw`, `Tz`, `TL`, `Tf`, and `Ts` control
spacing, scaling, leading, font selection and rise. Text state is saved by
`q`/`Q` and retained across text objects; `BT` resets the two text matrices.
Text matrices themselves are not saved by `q`/`Q`.

All eight `Tr` modes apply to supported outlines and fallback glyph shapes: fill, stroke,
fill/stroke, invisible, and their clipping variants. Clipping accumulates the
nonzero winding of nonempty bitmap glyph outlines and intersects the graphics clip at `ET`.
Showing only spaces in a clipping text mode produces an empty text clip. Invisible
text still advances. Newline bytes inside strings are character codes, not line
positioning commands. Word spacing applies only to a single-byte code 32,
regardless of its Unicode mapping.

Simple-font `FirstChar`, `LastChar`, and direct or indirect `Widths` arrays
supply horizontal advances in thousandths of an em. `MissingWidth` supplies the
fallback outside that range. Identity CID metrics and vertical positioning are
described below. The standard 14 Type 1 fonts use pinned Adobe metrics when
PDF widths are absent, with bundled CFF outlines and selected extended TrueType
glyphs. Recognized nonembedded Arial, Times New Roman and Courier New family names
use pinned vector substitutes. Remaining glyph gaps, general font substitution,
hinting, custom Type 1 procedures and complete font fidelity remain separate work. Invalid required glyph metadata fails the page; invalid
ToUnicode metadata is isolated from valid glyph data. Bitmap fallback shapes
are not equivalent to actual font outlines.

Text clipping uses the page's bounded clipping allocations and work budget.
Transforms must remain finite. Zero font size and zero horizontal scale do not
paint; negative values reflect the glyph geometry.

## PDF Type 3 glyph programs

Type 3 fonts use named simple-font encodings or Encoding dictionaries with
BaseEncoding/Differences to select their CharProcs streams. FontMatrix maps glyph space into the full text transform. Widths are
interpreted in glyph space; only the horizontal component of the transformed
advance is used. Missing glyph names paint nothing and still use the declared
advance. ToUnicode does not replace the Encoding-to-CharProcs mapping.

The first operator must be `d0` or `d1`, with a zero vertical width and a width
consistent with the font dictionary. `d0` permits coloured graphics. `d1`
builds a shape, ignores colour-setting operators, and permits
image masks but rejects ordinary colour images. The caller's nonstroking colour,
pattern, mask and opacity paint the completed d1 shape once. Only text rendering mode 3
suppresses a Type 3 program; the other text rendering modes do not replace its
paint operations.

Each invocation has an isolated graphics state and path. Font-local Resources
replace the caller's resource dictionary; absent Resources retain the caller's
scope. Nested fonts, forms, images, graphics states and supported shadings use
the shared resource resolver. Direct and indirect font dictionaries are accepted,
as are hexadecimal escapes in dictionary and glyph names.

Glyph execution is limited to 16 nested calls, 65,536 invocations per page and
the shared 64 MiB decoded-content budget. Reusing a stream under another font's
resources is not itself rejected as a cycle; depth and work limits bound actual
recursion. Type 3 dictionaries are limited to 64 KiB and Differences arrays to
1,024 entries. Named encodings use the shared simple-font resolver. Glyph
programs can only use implemented PDF content operators. Invalid required font
metadata fails the page rather than changing the font to a bitmap fallback.

## PDF tiling patterns

Coloured and uncoloured Type 1 tiling patterns can paint fills, strokes, text and
image masks. `/Pattern` selects a coloured pattern space; a named resource such
as `[/Pattern /DeviceRGB]` supplies the base colour for an uncoloured pattern.
Gray, RGB, CMYK and Indexed base spaces are supported. `scn`/`SCN` choose the
pattern and its optional base-colour operands. Ordinary colour operators clear
the pattern selection, and `q`/`Q` preserve it.

Cells are clipped to BBox and repeated using XStep and YStep, including negative
steps, gaps and overlap. Matrix controls phase and affine placement. The pattern
is anchored to its parent content stream's initial coordinates, so later `cm`
operators do not move it. Form and glyph resource scopes retain their own initial
transforms. Tiling types 1 and 3 use rounded device-grid step vectors; type 2 keeps
the requested geometry. Singular original or rounded grids are rejected.

Normal-blended colored cells use floating-point color, shape and alpha with an
RGBA mirror. Cached definitions are rebuilt when the destination changes between
RGB and grayscale groups. Cells that request non-Normal blending are evaluated per painted object
against its actual backdrop, with all tiles in one non-isolated group. The
caller's transparency applies once to the resulting pattern source. A mask
definition inside a pattern has its own fixed backdrop. See [PDF patterns](pdf-patterns.md)
for the evaluation rules, tests, limits and remaining color-cache work.

Uncoloured cells reject colour-setting operators and ordinary colour images;
image masks and transparency parameters are allowed. Resources are local to the
pattern dictionary. Limits are eight nested patterns, 65,536 cached instances per
page, 64 MiB of definitions, cached cells, per-object surfaces and descriptors, 4,096 pixels per cell axis, four
million pixels per cell, and 4,096 overlapping copies per period. The existing
shared content and invocation budgets also apply. Empty bounding boxes produce
transparent cells without entering a large replication loop.

Stroke coverage now measures width through the current affine transform,
including nonuniform scale and shear; zero-width hairlines use device-pixel
coverage. Cap, join and dash handling is described below.

## PDF shading patterns

Type 2 patterns support function-based, axial and radial shadings (ShadingType
1, 2 and 3), free-form/lattice triangle meshes (types 4 and 5), and
Coons/tensor-product patches (types 6 and 7).
Their Matrix is relative to the parent content's initial transform.
The shading's Background fills areas outside its domain, subject to its BBox;
the direct `sh` operator ignores Background and the current fill/stroke pattern.

Each gradient is cached as a transparent viewport-sized raster. Repeated uses
of an indirect pattern resource reuse that raster when the viewport and initial
transform and initial AIS setting match. Their colors are rebuilt when the
destination changes between RGB and grayscale. These rasters share the 64 MiB pattern-cache budget, while the
tiling-only cell dimension limits do not apply. The caller's alpha and blend
mode apply when the pattern paints. Its own ExtGState separately supports ca,
BM, SMask and AIS. Parameters for strokes, fonts and text do not affect the
internal shading operation. Non-Normal patterns use a non-isolated knockout
group against the object's backdrop; cached Normal patterns retain color, shape
and alpha separately. The cache distinguishes different initial AIS settings.
Patch rendering has bounded subdivision and inverse-mapping work. See [PDF shading](pdf-shading.md)
for mesh formats, interpolation, memory bounds and comparison limits.

## PDF stroke styling

`J`, `j`, `M` and `d` control butt/round/projecting-square caps, miter/round/bevel
joins, miter limits and dash arrays. Defaults are butt caps, miter joins, a miter
limit of 10 and a solid line. Graphics-state dictionaries accept LC, LJ, ML and D,
including indirect dash arrays, phases and numeric entries. `q`/`Q` preserve them.

Dash lengths follow the current user-space metric, including nonuniform scale
and shear. Odd arrays repeat twice to complete a cycle. The phase restarts for
each subpath, while dashes continue through corners and flattened curves. Zero
lengths are allowed when the array has a positive total; round zero-length dashes
paint dots. Closed dashed paths retain capped endpoints at the traversal seam.
Solid closed paths join there. Degenerate butt/square subpaths do not paint;
round-capped ones paint a circle.

All stroke primitives share one coverage mask, so caps, joins and overlapping
segments do not multiply opacity. Limits are 32 dash entries, 65,536 expansion
steps/fragments and 262,144 primitives per stroke, plus the shared pixel-work
budget. Invalid arrays and exhausted budgets fail explicitly. Curve flattening
and pixel-centre coverage remain approximations; this is not antialiased outline
parity. Automatic stroke adjustment is not implemented.

## CID metrics and vertical text

Type 0 fonts with Identity-H or Identity-V encodings consume big-endian,
two-byte character codes. Embedded encoding CMaps also map variable-length source
codes to CIDs. DW/W supply horizontal advances; DW2/W2 supply vertical
advances and origin offsets. Both consecutive-width arrays and CID ranges are
supported, with indirect arrays and numeric values. Without overrides, horizontal
width is 1000, vertical advance is -1000, vertical origin Y is 880, and origin X
is half the horizontal width. Metrics are in thousandths of an em.

Vertical text applies its origin offset before painting and advances along the
text matrix's Y axis. Character spacing and TJ adjustments follow that axis;
horizontal scaling affects glyph width and the horizontal origin offset, not the
vertical advance. Word spacing does not apply to these two-byte codes. Text
painting/clipping modes and saved graphics state use the same placement rules.

ToUnicode chooses a fallback glyph but does not choose the CID's metrics. An
unmapped CID shows the existing placeholder, rather than treating its number as
Unicode. Invalid and truncated codes use CID zero with codespace-based byte
recovery. Invalid required font metadata fails the page; optional extraction-map
failures do not discard a valid font program. Supported embedded TrueType programs instead
select outlines through CIDToGIDMap; CFF CID programs use their charsets and
font dictionaries. Embedded maps support CID ranges, notdef
recovery and UseCMap inheritance. The 59 predefined CJK maps in ISO 32000-1 Table 118 are bundled along with
Identity-H/V. Complete CJK text rendering still depends on the remaining font
program and fallback work.

Metric tables are bounded to the 65,536-CID space, with at most 65,536 assignments
per W/W2 array, 16 levels of indirect-value resolution and 64 MiB of CID metadata
per collected font table. Allocations are released on metadata failure and after
rendering.

Embedded program details, limits and tests are in [PDF fonts and text](pdf-fonts.md).
