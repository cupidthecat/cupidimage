# PDF transparency

Transparency support is incomplete. The renderer handles device-RGB compositing,
the 16 standard blend modes, image masks, graphics-state soft masks and
RGB/gray transparency Form XObjects. Unsupported group
configurations are rejected rather than rendered as ordinary opaque forms.

## Group Form XObjects

A form with Group subtype Transparency renders its members together. K selects
knockout behavior independently of isolation.
An isolated group (I true) starts on a transparent backdrop. A non-isolated group
(I false, the default) starts with the parent's accumulated backdrop. On entry, both alpha
constants reset to 1, the blend mode resets to Normal and the soft mask resets
to None. The caller's mask applies once when the finished group is painted. The finished group is
then painted once using the caller's nonstroking alpha and blend mode. The caller's
state is unchanged, and each invocation renders the group again.

This distinction matters when members overlap. Applying a group opacity to each
member separately darkens their overlap; applying it once to the completed group
does not. Nested groups, text, images, patterns and shading use the same group
surface. Matrix transforms, form bounds and clipping still apply.

Non-isolated groups track their own accumulated alpha separately from the initial
backdrop. Before the group is painted into its parent, that initial contribution
is removed according to ISO 32000-1 section 11.4.4. It is not painted a second time.
Colors and alpha remain in double precision through nested groups and are rounded
only when composited into an eight-bit page or cached pattern surface. This avoids
rounding errors being amplified by backdrop removal. Pattern and shading cache
construction does not modify the containing group's alpha.

Supported blending spaces are DeviceRGB and DeviceGray, explicit or inherited.
Explicit spaces are complete color-space objects, not ColorSpace resource aliases.
Built-in names, single-element arrays and indirect objects are supported. A non-isolated
group must use its parent's blending space. An isolated group may change it.
DeviceRGB converts to gray using 0.30 R + 0.59 G + 0.11 B; gray converts to
equal RGB components. These are device conversions, not color management.
DeviceCMYK converts directly to gray as
`1 - min(1, 0.30 C + 0.59 M + 0.11 Y + K)`, rather than passing through RGB.
Clipping RGB channels before that conversion would lose part of the ink contribution.

Solid fill/stroke colors, decoded image components, and all seven shading types
retain floating-point device values until compositing. Indexed colors expand to
their base components first. Normal-blended uncolored tiling patterns cache
coverage and alpha, then apply the caller's native color once. Backdrop-dependent
patterns retain double-precision per-object results. Ordinary colored caches also retain double-precision color and rebuild from their
definitions when the destination changes between RGB and grayscale.

Attributes may be indirect. A null optional entry is treated as absent; a null
Group entry leaves an ordinary form. DefaultRGB, DefaultGray and DefaultCMYK
remap painted source colors. A group without an explicit CS inherits its parent
blending space; an explicit CS is remapped and must still resolve to a supported
blending space. Calibrated and ICC blending spaces remain unsupported.

## Knockout and shape

K true makes each member blend against the group's initial backdrop rather than
against previous members. A non-isolated child of a knockout group also inherits
that initial backdrop. Shape and opacity are tracked separately using the
compositing equations in ISO 32000-1 section 11.4.8. A zero-opacity object can
still replace earlier members where its shape is nonzero.

ExtGState AIS chooses whether alpha constants and soft masks contribute to shape
(true) or opacity (false, the default). It is saved by q/Q and accepts indirect
boolean values. Image-local soft masks follow AIS; explicit binary and color-key
image masks constrain intrinsic shape. Nested groups carry their accumulated
shape even when their resulting alpha is zero.

Tiling and shading caches retain separate double-precision shape and alpha
planes alongside a byte image mirror; colored caches also retain double-precision
color. Fully transparent painted areas no longer
look like unpainted holes to a containing knockout group. Keeping cache alpha
unrounded also corrects three analytic pattern expectations by one byte; the
checked-in assets are unchanged. Group surfaces use 44 bytes per pixel, and
uncolored pattern caches use 20 bytes per pixel plus their descriptors. Colored
pattern caches use 44 bytes per pixel.
Backdrop-dependent pattern results use 44 bytes per pixel and are released after
the paint operation. See [PDF patterns](pdf-patterns.md).

## Text objects and Type 3 glyphs

Text knockout (ExtGState TK) defaults to true. All glyphs between BT and ET form
one non-isolated knockout group, so a later overlapping glyph replaces earlier
glyphs rather than increasing their opacity. TK false makes each glyph a separate
object. Transparency state applies to individual glyphs and persists after ET;
the implicit text group does not reset or restore that state. Its final composite
uses Normal blending with unit alpha and no additional mask or clip. Text clipping
is applied after that composite. Changing TK during a text object is rejected, as
are nested or unterminated text objects.

Each Type 3 glyph is collected as one object. A d0 glyph may contain multiple
colored paint operations that composite internally. A d1 glyph supplies only a
shape: its internal operations build that shape without the caller's color,
pattern, mask or opacity, then the caller's nonstroking paint is applied once.
Overlapping operations inside a d1 glyph therefore do not double its opacity.
Invisible and missing glyphs do not contribute shape.

Implicit text and glyph buffers share the 256 MiB transparency limit and pixel
budget. Text groups also consume the invocation budget. Allocation failures and
malformed glyph/text streams release these buffers without retaining a partial
text object.

## Displayed page groups

Every displayed page starts as an isolated transparency group, including pages
without an explicit Group dictionary. Content blends against transparent pixels,
not against the white display background. Only the finished page is composited
onto white with Normal blending and full opacity. In particular, Screen or
Difference on the first object cannot blend against white prematurely.

A page's Group dictionary selects DeviceRGB or DeviceGray, including direct and
indirect color-space objects. Its I flag does not change display isolation. Group is not
inherited from the Pages tree. Unsupported group spaces are rejected,
including on empty pages. Page attributes are checked before content is rendered.
The final medium composite ignores the last object's mask, clipping path, alpha
and blend mode. Page buffers share the transparency-memory budget with nested
groups and masks.

## Graphics-state soft masks

ExtGState SMask dictionaries support Alpha and Luminosity masks derived from
supported transparency forms. The mask captures the current transform when the
graphics state is set, including the form's Matrix. Later transforms do not move
it. q/Q saves and restores the mask; None clears it and null leaves it unchanged.
A new mask replaces the previous one rather than intersecting it.

Alpha uses the group's accumulated opacity. Luminosity composites the group
against BC, which defaults to black, then converts its color to gray. Luminosity
requires an explicit group blending space. BC is ignored for Alpha. Isolated
mask groups blend their members without BC; non-isolated groups blend against BC.
The resulting mask passes through TR, either Identity or a supported one-input,
one-output function of type 0, 2, 3 or 4. Output values are clamped to [0, 1].

The mask multiplies paint opacity, including patterns, text and ordinary forms.
Transparency groups clear it while rendering members and apply it once to the
completed group. Combined fill/stroke coverage also applies it once. An image's
own SMask or Mask replaces the graphics-state mask for that image only; it does
not clear the saved graphics state. Stencil images still use the current mask.

Masks are reference-counted across saved states and nested forms. Each new gs
invocation renders its mask again; there is no cross-invocation mask cache.
Recursive definitions, malformed functions and exhausted budgets return an
error. Mask evaluation uses the shared function-evaluation and pixel-work budgets.

## Limits

- CMYK, CIE-based and ICC group blending spaces are not implemented.
  An omitted I flag uses the supported non-isolated behavior.
- Overprinting remains incomplete. Additional color spaces and device color
  management are still separate work.
- Group buffers keep floating-point RGB, accumulated alpha and shape, plus an RGBA8
  mirror for existing raster surfaces. They are not wide-gamut color-managed
  surfaces.
- Group buffers and retained double-precision soft masks share a 256 MiB limit.
  Buffers currently
  span the full output page, so even a small group can reach that bound on a large
  page. Groups also share the form nesting, invocation and pixel-work budgets.
- A failed group is discarded before it is composited onto its parent. The
  allocation-failure tests check that its parent pixels and allocations remain
  unchanged on failure.

## Verification

Run:

    make test-pdf-text-knockout test-pdf-knockout test-pdf-page-groups test-pdf-groups test-pdf-soft-masks
    python3 scripts/test_pdf_text_knockout.py bin/test_pdf_pages --reference
    python3 scripts/test_pdf_knockout.py bin/test_pdf_pages --reference
    python3 scripts/test_pdf_page_groups.py bin/test_pdf_pages --reference
    python3 scripts/test_pdf_groups.py bin/test_pdf_pages --reference
    python3 scripts/test_pdf_soft_masks.py bin/test_pdf_pages --reference

The suite has 81 cases and 65 optional independent comparisons. It checks opacity
application, all 16 blend modes at two member opacities, backdrop removal, nested
groups, transparent backdrops, very small alpha values, strokes, clipping, inherited and
indirect attributes, text, images, patterns, shading and rejected configurations.
All expected pixel images are analytic. Selected alpha comparisons allow one
RGB unit for intermediate eight-bit rounding; other comparisons are exact.

The bounding-box case has an analytic oracle only because the independent
rasterizers round group bounds differently. Other comparison fixtures avoid
edge ties so they test compositing rather than differing boundary conventions.
RGB-to-gray conversion and gray-group composition use analytic expectations
because the independent renderers use different color-conversion behavior.

The page-group suite has 93 cases and 84 optional independent comparisons.
It covers all 16 initial blend modes, default and explicit isolation, gray
blending, indirect resources, empty pages and final-state separation. Gray
conversion cases use the same analytic-only device conversion noted above.
Two assets generated by `scripts/generate_pdf_page_group_fixtures.py` check
initial Screen blending and a gray page with I false.

The knockout suite has 73 exact analytic checks, including all 16 blend modes,
both isolation modes, AIS, zero-opacity shape, masks, patterns, nested backdrops,
page groups and transparent strokes. Its 16 optional independent comparisons use
the secondary rasterizer. Other cases remain analytic-only: the independent
implementations disagree on knockout color, AIS, zero-opacity members and page K,
and some intermediate-alpha outputs differ by more than one byte. The comparison
tolerance remains one RGB unit. Two generated assets distinguish shape from
opacity; use `scripts/generate_pdf_knockout_fixtures.py` to rebuild them.

The text-knockout suite has 36 checks and 14 optional independent comparisons.
The remaining valid cases have exact analytic expectations: the reference
implementations tested here do not honor the specified text-object knockout
behavior for the overlapping TrueType and Type 3 fixtures. Tests distinguish
that behavior from TK false, separate text objects, d0 internal compositing and
d1 shape-only painting. Two assets are generated by
`scripts/generate_pdf_text_knockout_fixtures.py`.

The soft-mask suite has 56 checks and 44 optional independent comparisons.
Five valid cases use analytic expectations only: transfer-output clamping,
restoration after an image-local mask, color-key mask precedence, combined
fill/stroke coverage and a non-isolated luminosity mask blending against BC.
The independent renderers differ on those cases; their output is not treated as
an override of the specification. The checked pixels remain exact in analytic
tests; independent mask comparisons allow at most one RGB unit.
These checks do not establish general transparency or whole-document parity.

The isolated-opacity asset is generated by
`scripts/generate_pdf_group_fixture.py`; the non-isolated backdrop asset uses
`scripts/generate_pdf_nonisolated_fixture.py`. The isolated fixture applies outer
opacity once to overlapping members; the non-isolated fixture distinguishes
group alpha from backdrop alpha. The PDF allocation-failure suite has 2,586 checks, including
group/mask allocation, transfer functions, malformed content, nesting and
memory-budget failures. Three additional analytic mask assets are generated by
`scripts/generate_pdf_soft_mask_fixtures.py`.

## Specification

The group implementation follows [ISO 32000-1](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf),
sections 9.3.8, 9.6.5, 10.3.2, 11.4, 11.5, 11.6.4.3, 11.6.4.4, 11.6.5 and 11.6.6, including the group attributes in Table 147.
Image masks, graphics-state soft masks, page groups and transparency Form XObjects
are distinct mechanisms; supporting one does not establish support for the others.

## Graphics-state values

Blend-mode names decode PDF name escapes. BM accepts direct or indirect names and
arrays, including indirect array elements, choosing the first supported mode.
Arrays are bounded to 64 entries. Explicit null entries, including references
resolving to null, leave existing opacity, blend, line and dash state unchanged.
Malformed names, invalid types and reference cycles are rejected. Dangling
optional references still use strict object lookup rather than null substitution.

Shading-pattern state follows the same decoding rules for its applicable
parameters; see [PDF patterns](pdf-patterns.md). The general transparency suite
now has 177 checks, in addition to the focused group, mask and pattern suites.

## Surface bounds

Groups track the bounds of contributed shape, not just nonzero alpha. Completion
therefore preserves transparent knockout objects while skipping untouched pixels.
A supplied background initializes full-surface bounds. Implicit text groups defer
backdrop initialization until a pixel is painted; other group surfaces retain
their existing initialization. Nested backdrop lookup handles both forms.


## Combined fill and stroke

The path operators B, B*, b and b*, and glyph rendering modes 2 and 6, paint a
single object. Without the still-unimplemented overprint special case, ISO
32000-1 section 11.7.4.4 defines that object as a non-isolated knockout group.
Its fill and stroke retain their respective alpha constants and blend mode;
the completed object is composited with unit alpha and Normal blending.
This grouping is independent of the text-knockout setting.

Stroke precedence alone is sufficient when the stroke contributes full shape.
It is not sufficient for alpha-as-shape strokes or patterned strokes with holes
or partial shape. Those cases now retain the fill where stroke shape is absent
or fractional. `pdf_paint_groups.c` supplies a bounded, lazily initialized implicit
surface shared by paths, outline glyphs and the remaining bitmap fallback. It
composites only the contributed bounds and releases the surface after each object.
The existing transparency-memory budget applies.

The graphics-state soft mask applies once to the completed object. It is cleared
while the fill and stroke members are rendered, then restored for compositing.
State, mask ownership and text-clipping accumulation survive the temporary group.
Opaque/simple cases keep the existing direct path without an extra surface.

`make test-pdf-combined-paint` compares combined operations with explicit knockout
groups for TrueType, Type 1, CFF and fallback glyphs, patterns, masks, clipping,
blend modes and nested groups. Separate interior-pixel equations and three analytic
assets check the expected colors. Allocation-failure tests cover restoration and
cleanup. The optional `--reference` pass checks selected interior color samples,
not complete images or raster-edge equivalence.

Independent implementations still differ on fractional AIS, text knockout and
some pattern placement. In particular, ordinary sequential alpha compositing
is not substituted for the specified knockout equations merely to match a renderer.
Those differing configurations retain explicit-group and analytic oracles rather
than being counted as independent matches.

    python3 scripts/test_pdf_combined_paint.py bin/test_pdf_pages bin/test_pdf_clip_oom --reference

Regenerate the three owned assets with `scripts/generate_pdf_combined_fixtures.py`.


Outline and bitmap text now start their implicit group only when a glyph has
drawable geometry. Outline-less glyphs neither allocate that surface nor establish
a text clip. See the [font guide](pdf-fonts.md#glyphs-without-outlines) for the
specified clipping exception and the comparison renderer's differing behavior.
