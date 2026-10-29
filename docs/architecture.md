# Source architecture

The installed API remains `src/cupidimage.h`. Implementation headers stay under
`src/` and are private; applications should not depend on their types or helper
symbols.

## Shared code and outputs

- `src/core/image.c` owns decoded-image cleanup.
- `src/core/io.c` handles file reads, loader wrappers, and shared errors.
- `src/core/load.c` detects formats and dispatches to codecs. This logic is no
  longer part of the JPEG decoder.
- `src/compression/zlib.c` supplies checked DEFLATE/zlib decoding and terminal
  compression. PNG, TIFF, and PDF share this implementation.
- `src/compression/ccitt.c` handles bounded PDF fax streams; its run-code tables
  are shared with the TIFF strip decoder.
- `src/output/ansi/`, `kitty/`, and `sixel/` own terminal protocols. They consume
  RGBA images rather than parsing source-file formats.
- `src/cli/main.c` owns argument parsing, terminal selection, animation playback,
  and PDF page iteration.

## Format modules

Each codec lives under `src/formats/<format>/`. Smaller codecs remain one focused
translation unit. Larger ones have private contracts and separate responsibilities:

- JPEG: `cupidimage_jpeg.c` handles markers, frame/scan lifecycle and the public
  wrappers. `jpeg_entropy.c` owns Huffman tables and shared sequential/progressive
  coefficient decoding. `jpeg_arithmetic.c` implements arithmetic probability
  states and sequential/progressive coefficient scans. `jpeg_output.c` reconstructs
  samples and provides native component or RGBA output. `jpeg_internal.h` defines their private contracts.
- PDF: public page APIs, document/object parsing and streams, font maps and the
  bitmap fallback, image samples and masks, and graphics/content rendering.
  `pdf_xref.c` builds revision-aware indices; `pdf_objects.c` resolves indexed
  objects and stream parameters with shared recursion/work budgets. The immutable
  index is passed through document, resource and font views without global state.
  Type 3 font metadata lives in `pdf_font_type3.c`, and shared resource-scope
  lookup lives in `pdf_resources.c`. Pattern validation and the bounded
  per-page cell/gradient cache live in `pdf_patterns.c`. Pattern stream execution
  and per-object backdrop evaluation live in `pdf_pattern_render.c`. Device-space path construction and deferred curve subdivision live in
  `pdf_path.c`. Stroke geometry, dash
  expansion and bounded union coverage live in `pdf_stroke.c`. CID
  horizontal/vertical metrics are parsed in `pdf_font_cid.c`, separately from
  Unicode maps and glyph rasterization. `fonts/` holds binary font tables,
  encoding data, the bounded PDF encoding CMap parser and the bitmap fallback. `pdf_font_program.c` connects embedded
  programs to PDF resources; `pdf_font_substitution.c` owns pinned substitute loading
  and recognized nonembedded-family selection. `pdf_font_truetype.c` handles PDF encoding selection
  and glyph-name fallback for embedded TrueType programs. `pdf_text_outline.c` handles quadratic/cubic glyph
  coverage and text clipping. `fonts/cff.c` reads compact font data, and
  `fonts/type2.c` interprets its bounded charstrings. `fonts/type1.c` reads and
  decrypts Type 1 programs; `fonts/type1_charstring.c` interprets their outlines
  and standard subroutine operations. Shared simple-font encoding resolution
  lives in `pdf_font_encoding.c`; `pdf_standard_font.c` applies the immutable
  standard-font metrics from `fonts/standard.c` and CFF substitutes from
  `fonts/standard_outlines.c`. TrueType binary geometry remains in
  `fonts/truetype.c`; cmap lookup and post names live in
  `fonts/truetype_cmap.c` and `fonts/truetype_names.c`. Font tables collect both
  named fonts and ExtGState font references before drawing. Graphics-state font
  references use a separate object-keyed lookup so they cannot collide with
  resource names. `pdf_object_values.c` resolves bounded indirect values and
  names independently of fonts. `pdf_groups.c` manages floating-point RGB/gray group buffers with separate shape and alpha,
  state initialization and compositing, including the isolated page buffer and
  final white-medium composite. Knockout members use the initial group backdrop.
  Pattern caches retain floating-point color, shape and alpha separately. `pdf_soft_masks.c` renders group-derived
  masks, evaluates their transfer functions and owns their reference-counted
  lifetime. Groups and retained masks share a transparency-memory budget.
  Text matrices, glyph coverage, spacing and advances live in `pdf_text.c`;
  text parameters are part of the saved graphics state. `pdf_text_groups.c` owns
  implicit text/glyph surfaces, neutral final compositing and Type 3 stencil paint.
  `pdf_calibrated.c` validates CalGray/CalRGB/Lab parameters and converts calibrated
  source samples through CIE XYZ to the fixed sRGB output. `pdf_icc.c` owns
  bounded matrix/tone-curve profile parsing and the per-page immutable profile cache.
  `pdf_content_syntax.c` decodes names and text strings and scans bounded aggregate
  operands without executing their contents. The operator executor validates TJ
  element types and text-object context.
  Byte filters and their bounded pipeline live in `pdf_filters.c`; image profiles
  add DCT decoding without forcing native CMYK data through RGBA. The private
  JPEG sample entry point shares the existing entropy and IDCT implementation.
  `pdf_objects.c` resolves only the parameters applicable to each filter stage.
  Byte-filter dispatch remains separate from image color conversion.
  ExtGState parsing and blend functions live in `pdf_transparency.c`, and
  function-based/axial/radial shading lives in `pdf_shading.c`. `pdf_mesh.c`
  decodes byte-aligned triangle streams, interpolates samples and composites each
  complete mesh once. `pdf_mesh_internal.h` shares the bounded sample context with
  `pdf_patch.c`, which decodes patch edges, subdivides control nets, solves inverse
  mappings and retains per-pixel parameter priority. Bounded color-function trees
  are parsed and evaluated in `pdf_functions.c`.
  Inline image syntax and encoded-data boundaries live in `pdf_inline.c`.
  Inline images and XObjects share the borrowed image-view renderer.
  Image XObjects live in `pdf_images.c`, with palette parsing in `pdf_image_colors.c`; the private render-state contract lives
  in `pdf_render_internal.h`.
- SVG: public APIs, parsing, styles, definitions, CSS, geometry, paint, text,
  rasterization, render orchestration, filter parsing, filter execution, and blur.
  Shared private scene and style types live in `svg_internal.h`.
- TIFF: public loading, directory/tag parsing, compression, strip/tile raster
  assembly, and color/orientation conversion.
- WebP: RIFF/alpha handling, VP8 decoding, VP8L decoding, and coding tables.

Some formats intentionally call other codecs: ICO can contain PNG; TIFF and PDF
can contain JPEG; SVG can contain embedded raster images. Those dependencies
reuse decoders rather than adding separate copies. Terminal outputs do not depend
on a particular input format.

Module boundaries do not imply full format conformance. For example, the PDF font
module still uses a limited bitmap fallback rather than a complete font engine.
See [formats](formats.md) and the [conformance tracker](format-conformance.md).

## Build and tests

The Makefile discovers library C files recursively, excluding `src/cli/`, and
keeps matching subdirectories under the object directory. A source-list dependency
tracks additions/removals. The static archive is recreated when rebuilt so stale
members from a previous layout cannot survive.

Every production module compiles separately. The allocation-failure tests
intentionally include the relevant implementation modules under fault-injection
macros; that is test-only instrumentation, not the production build model.

Refactors must preserve the public header, reference-pixel tests, malformed-input
behavior, and output protocols. The asset runner also records full decoded pixel
hashes, which can be compared before and after a behavior-preserving refactor.
These hashes show behavior preservation, not independent visual correctness.


### Font-space geometry reuse

`pdf_font_outline.c` owns the bounded decoded-outline cache for each native font
program. `pdf_text_outline.c` builds transformed edges without modifying cached
font coordinates. Paint state stays in the renderer, and the page budget bounds
retained outline storage separately from font-program bytes. Font destruction
releases cache entries.

ICC profile ownership and tag selection live in `pdf_icc.c`; bounded forward
lookup-table evaluation lives in `pdf_icc_lut.c`. Both use the shared calibrated
XYZ-to-output conversion. Raw tables borrow immutable profile storage, so cache
cleanup has one owner and no per-pixel allocation.

`pdf_intents.c` parses PDF intent state. The graphics state carries the selection;
`pdf_icc.c` keeps separate immutable per-intent transforms in each cached profile.
The calibrated source-to-XYZ step is shared with ICC conversion before the final
output transform, preserving PCS range and reference-medium adjustments.

ICC tone curves live in `pdf_icc_curve.c`; `pdf_icc_mab.c` composes those curves,
shared CLUT evaluation and matrix offsets for version 4 forward transforms.
The old table and newer stage models share bounded parsing and immutable profile
storage without duplicating the curve equations or interpolation code.

`pdf_paint_groups.c` owns temporary combined fill/stroke groups. Path and glyph
renderers retain geometry and clipping ownership; the helper borrows the caller's
mask, redirects painting into a bounded surface, then restores state and applies
the mask once to the completed object.

Standard-font programs can select a bounded fallback chain: compact CFF outlines,
extended TrueType outlines, then the spacing-mark CFF subset. Encoded glyph tags
select the program before outline lookup. Each program owns its bytes/cache and
contributes to the same page budget; AFM/PDF widths remain outside that selection.

Unhinted TrueType outlines carry their effective advance as well as points. The
component walk resolves USE_MY_METRICS once; fallback-width lookup reuses the same
bounded outline cache as painting. Glyphs without metric inheritance retain the direct metric lookup.
Raw font metrics remain separate from component-inherited metrics and PDF overrides.

`fonts/truetype_vm` separates instruction dispatch/control flow from stack and
storage operations, fixed-point arithmetic/rounding, graphics-state setters and
axis-size preparation and projection-dependent CVT access, vector normalization, point zones, original design coordinates, movement/interpolation operators, exact line intersections, curve flags and
prepared-state lifetimes. Instances own their mutable state but borrow immutable program
bodies. This foundation is currently exercised by tests, not the page renderer;
see [its scope and ownership rules](pdf-truetype-vm.md) before integrating hinting.
`fonts/truetype_hint` connects the shared outline/component decoders to size preparation,
phantom construction, bounded composite assembly and glyph execution. Its owned VM borrows the checked font's
backing bytes; [the loader guide](pdf-truetype-loader.md) describes that lifetime and
component state sharing. Its final affine adapter returns owned device-pixel
points without altering the instruction state or PDF advances.
