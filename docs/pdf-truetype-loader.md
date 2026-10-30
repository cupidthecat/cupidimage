# Native TrueType glyph loading

The native hinting loader connects simple/composite glyph decoding to the
[instruction engine](pdf-truetype-vm.md). It is tested separately from PDF rendering.
Default PDF pages still use unhinted outlines. An explicit [page option](pdf-page-hinting.md)
now connects this loader to device outlines and coverage. Broader font-program and
modern-profile qualification remain unfinished.

## Ownership and preparation

`fonts/truetype_hint` owns a VM and a reusable raw-outline buffer. The input
`pdf_tt_font` must have been checked by `cupidimage_pdf_tt_init`. Table views and
instruction bodies borrow the font's backing bytes, which must outlive the hinting
instance. Free an instance before reinitializing it, including after a failed call.

Initialization requires a version-1 maximum-profile table and uses its stack,
storage, function and twilight limits. It reads signed control values, executes the
font program, establishes a uniform size, executes preparation and saves the
prepared state. Font and preparation execution share one work budget. Temporary
control values are freed after preparation; the VM owns its scaled values and
snapshots.

Each top-level glyph starts from the saved state. Twilight coordinates retain the VM's
specified lifetime across glyphs, while glyph points, stack contents and private
CVT/storage writes are replaced or restored. Within a composite, child, sibling and
parent instruction phases share private CVT/storage values. Component entry resets
graphics settings and geometry without discarding those private writes. Allocation failures leave an instance
safe to free, but not safe to execute again.

## Simple glyphs

The loader uses the same bounded simple-outline decoder as the unhinted path.
Coordinates remain in the font's design grid while flags, repeated entries, signed
deltas and contour ends are decoded. The decoder also returns a borrowed view of
the glyph's instruction stream. It accepts the full nonnegative signed-16-bit
contour range, up to 32,767 contours, within the existing 65,536-point bound.

The hinting path keeps design coordinates separate from scaled originals and
current device coordinates. It does not normalize the pen origin before executing
instructions. Glyph bytes and point setup consume the glyph work budget before the
remaining budget reaches the VM. Malformed descriptions, invalid indices, arithmetic
overflow and exhausted budgets fail explicitly.

An empty glyph can still contain instructions that operate on its phantom points.
Those instructions execute even when there are no outline contours. Composite
glyphs use a [separate assembly and instruction path](pdf-truetype-composites.md)
that processes children before their parent.

## Phantom points and metrics

Four phantom points follow the outline points:

- The first horizontal point is `xMin - leftSideBearing`; the second adds the
  horizontal advance width from `hmtx`.
- When `vhea` and `vmtx` are present, the first vertical point is
  `yMax + topSideBearing`; the second subtracts the advance height. Repeated trailing
  metrics use the last advance height and the glyph's own bearing.
- Without vertical metrics, the vertical points use the typographic ascender and
  descender from `OS/2`, or the ascender and descender from `hhea` if `OS/2` is absent. A shortened legacy version-0 table
  also uses this fallback because it has no typographic metrics.

Horizontal phantom y coordinates and vertical phantom x coordinates are zero in
this legacy uniform configuration. The original coordinates retain scaled
fractions. Current horizontal x and vertical y positions are rounded to pixel grid
lines before instruction execution, unless preparation inhibits hinting. Inhibition
also leaves the glyph instructions unexecuted.

Results remain owned by `vm.zones[1]`, with the four phantom points included in
`count` and excluded from `outline_count`. A consumer can normalize the final outline
by its first horizontal phantom point. These font metrics must not replace PDF
Widths, W/DW, W2/DW2 or authored TJ adjustments when rendering integration is added.

## Verification and limits

`make test-pdf-tt-loader` checks native extraction, state reuse, metric fallbacks,
phantom rounding, inhibition, empty-glyph instructions, malformed data, work limits
and allocation failures. It also checks a 32,767-contour description against the
shared decoder.

With the optional font test packages installed, native loading produces 9,330
matching simple-glyph outlines (coordinates, curve flags and contour boundaries) in each of monochrome and grayscale configurations
from the 12 pinned fonts at six sizes, selecting
Unicode mappings U+0020 through U+00FF. Another 576 authored-font cases probe
horizontal bearings, both original/current phantom coordinates, vertical metrics
and the `OS/2`/`hhea` fallbacks. Of these, 543 match independently and 33 shortened-table
cases differ. The tested comparison engines read typographic values from bytes
following the shortened `OS/2` table; changing the adjacent table changes their
phantom coordinates. Native loading stays within the declared table and uses the
`hhea` fallback. Those differences are reported separately, not accepted as matches.
The all-simple mode at 16 ppem covers 17,462 matching
outlines and four explicitly rejected IP programs with out-of-range reference points. Rejections are not
counted as matches or full font support.

Composite probes separately compare 4,242 selected outlines at six sizes in each
configuration, plus 12,596 outlines in the all-composite 16-ppem mode. Authored tests
cover nesting, transforms, metric inheritance, point attachments, private state,
malformed graphs and allocation failures. See the composite guide for exact scope
and known comparison differences.

Selected page integration and antialiased fills are now tested separately. Broader
instruction/variation qualification and complete raster fidelity remain open.
Existing unhinted defaults continue unchanged.

The data contracts follow the official [glyph table](https://learn.microsoft.com/en-us/typography/opentype/spec/glyf),
[maximum profile](https://learn.microsoft.com/en-us/typography/opentype/spec/maxp),
[horizontal metrics](https://learn.microsoft.com/en-us/typography/opentype/spec/hmtx)
and [vertical metrics](https://learn.microsoft.com/en-us/typography/opentype/spec/vmtx)
specifications. See [the font guide](pdf-fonts.md) for the wider PDF requirements.

## Axis sizes and transformed output

`cupidimage_pdf_tt_hint_init_xy` prepares independent device-axis sizes and honors
integer-ppem requests in the font's head flags. Points, phantom metrics and
component offsets use their own axis scales. Child instructions still run before
component transforms and parent instructions. The uniform initializer remains a
wrapper, and both paths preserve PDF-independent metric construction.

After loading, `cupidimage_pdf_tt_hint_shape_build` copies the outline through a
16.16 affine matrix and F26Dot6 translation into owned device-pixel points. The
source VM geometry is unchanged, and the copy survives release of the loader.
Free it with `cupidimage_pdf_tt_hint_shape_free`. Empty glyphs produce empty shapes;
work, allocation or coordinate overflow fails without returning a partial shape.
This adapter does not choose PDF text advances, rasterize, or connect the loader
to the page renderer. See the VM guide for comparison scope and differences.
