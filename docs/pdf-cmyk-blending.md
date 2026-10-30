# PDF CMYK blending

DeviceCMYK and supported bidirectional ICC CMYK profiles retain four native
components through transparency groups. K is not discarded by an early RGB
conversion. The completed group converts into its parent, and the final page
still uses the library's fixed RGB output target.

This covers isolated and non-isolated groups, knockout composition, images,
text, Type 3 glyph objects, combined fill/stroke painting, patterns and soft
masks. It does not add overprint, physical spot-color planes, monitor/output
profiles or black-point compensation.

## Blend equations

Separable blend modes complement each ink component, apply the additive blend
function, then complement its result. Nonseparable modes complement C, M and Y
and apply the RGB hue/saturation/luminosity equations to those three components.
Hue, Saturation and Color retain the backdrop's K; Luminosity takes the source's
K. The normal opacity and shape equations then combine all four result components.

The implementation follows ISO 32000-1 sections 11.3.4 and 11.3.5. Using a
three-channel RGB blend and converting it back would not preserve those ink values.

## Black generation and undercolor removal

DeviceRGB-to-DeviceCMYK conversion follows section 10.3.4. It first computes
C = 1 - R, M = 1 - G, Y = 1 - B and k = min(C, M, Y). It subtracts UCR(k) from
each chromatic ink and uses BG(k) as black, clipping final components to [0, 1].
Negative undercolor removal can add chromatic ink.

BG and UCR accept supported one-input/one-output PDF functions. BG2 and
UCR2 take precedence and also accept Default, which restores the page's
default function. The fixed output configuration uses identity defaults for both.
Functions of types 0, 2, 3 and 4 use the existing native function engine.

These functions are saved and restored with graphics state. An ordinary paint
uses its current functions; an RGB Form group entering a CMYK parent uses the
functions at Do. They do not alter DeviceGray-to-CMYK conversion, existing
CMYK components or an ICC profile's own destination tables. DeviceGray maps to
zero C/M/Y and K = 1 - gray.

The parsed functions live in a page-owned cache shared by nested contexts.
Repeated selections reuse immutable trees. The cache has a 16 MiB limit and at
most 256 entries. Key comparisons and evaluation use the shared work budget.
Malformed functions, exhausted budgets and allocation failures reject the page.

## Surfaces and caches

Gray and RGB group surfaces keep their existing 44-byte-per-pixel storage.
CMYK surfaces use 52 bytes per pixel for four floating-point components, alpha,
shape and the legacy byte mirror. Both share the existing 256 MiB transparency
limit. Uncolored pattern caches still use 20 bytes per pixel.

Colored pattern caches start with three or four component planes as needed.
A cache first used for RGB can grow when reused in a CMYK group. That growth is
charged to the 64 MiB pattern budget and checked for allocation failure. A grown
cache retains its larger allocation when returning to RGB, but its color encoding
and pixels are rebuilt. Shading cache keys also distinguish the parent content
stream's initial black-generation and undercolor-removal functions.

CIE-based ICC masks still use forward CIE Y for luminosity. DeviceCMYK masks use
the existing device-gray conversion, including K. Mask backdrop arrays therefore
have four components in a CMYK group. Alpha masks remain independent of color.

## Tests and limits

The test-pdf-cmyk-groups Make target runs 6,718 black-generation unit checks,
720 native four-component blend comparisons, 203 page tests and 488
allocation-failure checks.
The tests inspect native components as well as final RGB pixels, because different
ink combinations can produce the same RGB result. They include all 16 blend
modes, Type 3 glyphs, color-cache growth, sampled/calculator/stitching functions,
state restoration, group-invocation conversion and four-channel ICC tables.

The owned assets/pdf/cmyk-blending.pdf fixture shows opacity, Hue, BG/UCR and an
authored four-channel ICC group. Its complete raster is decoded-unverified.
Regenerate it with scripts/generate_pdf_cmyk_blend_fixture.py.

Device color conversions remain implementation-dependent where the specification
allows that choice. The fixed CMYK-to-RGB model is 1 - min(1, ink + K), not a
physical printer characterization. It can differ from renderers configured with
other device profiles. Multiprocess ICC elements, alternate output targets,
overprint and complete document fidelity remain unfinished.

A focused audit of two original corpus documents still reports page failures.
One reaches a stitching function whose first bound equals the lower domain
endpoint; the other exceeds the shared full-page group-buffer limit while
constructing nested masks. CMYK group admission alone does not resolve those
dependencies, and neither document is counted as a rendering success.
