# PDF mask and color-function work

The renderer keeps its existing memory and work limits while avoiding work that
cannot affect a displayed pixel. These changes cover isolated soft-mask surfaces,
clipped color evaluation and boundary values in stitching functions. They do not
make every document fit the limits or establish whole-page rendering parity.

## Isolated mask surfaces

An isolated soft-mask root renders directly into its prepared transparent surface.
It no longer allocates a second full-page transparency surface merely to compose
the root back into the first one. Nested Form groups still have their own surfaces.
The root's matrix, bounding box, resources, isolation and knockout behavior remain
in effect.

For a Luminosity mask, the completed native components are combined with BC before
conversion to CIE Y or device gray. The calculation retains shape and opacity
separately, using the same composition order as an opaque parent. Alpha masks use
the accumulated alpha without color conversion. Non-isolated roots retain the
backdrop-dependent path.

An omitted BC uses the color space's initial components. DeviceCMYK starts at
[0, 0, 0, 1]; supported ICCBased spaces start at all zeros. CS/cs selection follows
the same distinction, so a four-channel ICC space does not inherit DeviceCMYK's
initial K value.

The shared transparency limit remains 256 MiB. The owned four-level mask fixture
now fits without increasing that limit. A deeper CMYK mask stack is still rejected.
There is no cross-invocation mask cache.

## Clipped color evaluation

Color-space and tint conversion is skipped when the compositor's existing
visibility test will discard the pixel, or when intrinsic shape is zero. This
does not skip zero-opacity objects that still have shape, which can affect a
knockout group.

Type 1, axial and radial shading evaluate color functions only for visible
pixels. A discarded shading sample still consumes one work unit, so repeated
clipped shading cannot bypass the work limit. Visible samples retain the normal
function-node and calculator costs. The page's color-function budget is unchanged.

The clipped-work fixture repeatedly paints a small clipped region using an
expensive tint function. Shading and image variants produce the same pixels as
one invocation. The corresponding unclipped workload still reaches the limit.

## Stitching-function endpoints

ISO 32000-2:2020 section 7.10.4 clarifies that the first Bounds value may equal
the lower Domain endpoint. That degenerate first interval contains the endpoint;
the next interval is open on its left. The endpoint maps to the first Encode
value. The last interval retains its existing closed upper endpoint and
degenerate-bound handling.

Interior bounds must still increase strictly. Repeated first/interior bounds,
out-of-domain bounds, invalid component counts and function cycles are rejected.
Tests check exact endpoint sample positions, both degenerate endpoints and use
inside black-generation functions.

## Qualification and remaining work

The group-work target checks 40 pages and 198 allocation-failure cases, including
large and small nested masks, root knockout, Alpha/Luminosity behavior, backdrop
colors, invalid root flags, clipping and unchanged limit failures. Stitching
tests include 29 function cases. Existing mask, CMYK, calibrated and clipping
suites remain separate.

Two original CMYK corpus pages now proceed beyond their earlier group, mask and
color-function failures. They still encounter dense-path segment and geometric
clipping-work limits, so neither is counted as a completed page. The full manual
corpus review remains deferred.

The owned nested-mask-surfaces.pdf has an exact full-page oracle, also matched
by an independent 96-DPI raster. The clipped-shading-work.pdf asset remains
decoded-unverified: its geometry agrees, but the independent renderer maps its
CMYK black to a different display RGB value (maximum channel difference 35).
The focused native test uses the documented fixed device-conversion formula.
Regenerate both fixtures with scripts/generate_pdf_group_work_fixtures.py.

See [transparency](pdf-transparency.md), [CMYK blending](pdf-cmyk-blending.md),
[shading](pdf-shading.md) and the [standards index](pdf-standards.md).
