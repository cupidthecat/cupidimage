# PDF transparency

Transparency support is incomplete. The renderer handles device-RGB compositing,
the 16 standard blend modes, image masks, graphics-state soft masks and
non-knockout RGB/gray transparency Form XObjects. Unsupported group
configurations are rejected rather than rendered as ordinary opaque forms.

## Group Form XObjects

A form with Group subtype Transparency and K false renders its members together.
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

Supported blending spaces are DeviceRGB and DeviceGray, either explicit, named
through the group's current ColorSpace resources, or inherited. A non-isolated
group must use its parent's blending space. An isolated group may change it.
DeviceRGB converts to gray using 0.30 R + 0.59 G + 0.11 B; gray converts to
equal RGB components. These are device conversions, not color management.
Attributes may be indirect. A null optional entry is treated as absent; a null
Group entry leaves an ordinary form. DefaultRGB, DefaultGray and DefaultCMYK
remapping remain unsupported and are rejected for these groups.

## Displayed page groups

Every displayed page starts as an isolated transparency group, including pages
without an explicit Group dictionary. Content blends against transparent pixels,
not against the white display background. Only the finished page is composited
onto white with Normal blending and full opacity. In particular, Screen or
Difference on the first object cannot blend against white prematurely.

A page's Group dictionary selects DeviceRGB or DeviceGray, including named and
indirect spaces. Its I flag does not change display isolation. Group is not
inherited from the Pages tree. Unsupported group spaces and knockout are rejected,
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

- Knockout groups and CMYK, CIE-based or ICC group blending spaces are not implemented.
  An omitted I flag uses the supported non-isolated behavior.
- Overprinting, alpha-as-shape behavior and
  full text-knockout semantics remain incomplete.
- Group buffers keep floating-point RGB and accumulated alpha, plus an RGBA8
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

    make test-pdf-page-groups test-pdf-groups test-pdf-soft-masks
    python3 scripts/test_pdf_page_groups.py bin/test_pdf_pages --reference
    python3 scripts/test_pdf_groups.py bin/test_pdf_pages --reference
    python3 scripts/test_pdf_soft_masks.py bin/test_pdf_pages --reference

The suite has 82 cases and 66 optional independent comparisons. It checks opacity
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

The page-group suite has 94 cases and 84 optional independent comparisons.
It covers all 16 initial blend modes, default and explicit isolation, gray
blending, indirect resources, empty pages and final-state separation. Gray
conversion cases use the same analytic-only device conversion noted above.
Two assets generated by `scripts/generate_pdf_page_group_fixtures.py` check
initial Screen blending and a gray page with I false.

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
group alpha from backdrop alpha. The PDF allocation-failure suite has 1,716 checks, including
group/mask allocation, transfer functions, malformed content, nesting and
memory-budget failures. Three additional analytic mask assets are generated by
`scripts/generate_pdf_soft_mask_fixtures.py`.

## Specification

The group implementation follows [ISO 32000-1](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf),
sections 10.3.2, 11.4, 11.5, 11.6.4.3, 11.6.4.4, 11.6.5 and 11.6.6, including the group attributes in Table 147.
Image masks, graphics-state soft masks, page groups and transparency Form XObjects
are distinct mechanisms; supporting one does not establish support for the others.
