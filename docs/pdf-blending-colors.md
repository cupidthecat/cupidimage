# PDF blending color spaces

Transparency groups retain their native color components while their members
blend. Converting each member to display RGB first would change the result for
nonlinear or wide-gamut spaces. Conversion happens when a source enters a group
and when the finished group enters its parent.

The supported group spaces are DeviceGray, DeviceRGB, CalGray, CalRGB and
one- or three-component ICC profiles with usable matrix/TRC inverses or selected
B2A lookup transforms. Their PCS may be XYZ or Lab; blending still operates on
the profile's gray or RGB device components.
Lab itself is not a permitted PDF blending space. CMYK groups, multiprocess
destination transforms, output profiles and black-point compensation remain
unfinished. An unsupported group fails; it does not silently become DeviceRGB.

## Conversion and state

Calibrated and ICC colors pass through floating-point XYZ coordinates adapted to
D65. Those intermediate coordinates are not clipped to the display RGB gamut.
The destination conversion produces the group's native components. Existing
DeviceGray/DeviceRGB conversions keep their original arithmetic. The final
output target remains fixed sRGB, not a user-selected monitor profile.

A non-isolated group must have the same encoding as its parent. Isolated groups
may change spaces. Separate ICC objects with identical profile bytes share an
equivalence identifier after a bounded comparison. Component count alone does
not establish equivalence. Pattern caches also retain the actual blending space
and rebuild when it changes, even between two gray spaces.

Source conversion uses the painting operation's rendering intent. Conversion of
a completed Form group uses the intent at its `Do` invocation. Final page
conversion uses the page's initial intent, rather than the last `ri` operation in
its contents. Existing intent selection and version restrictions still apply;
version 2 absolute-colorimetric conversions remain unsupported.

Explicit device group spaces honor the resource dictionary's DefaultGray or
DefaultRGB substitution. An omitted group space inherits its parent. An omitted
page group space uses the output-device space; it does not imply DefaultRGB.

CIE-based luminosity masks use the group's CIE Y value, including media-white
scaling for supported absolute-colorimetric ICC conversion. Applying RGB
luminosity weights to a display-converted color would be a different operation.
Device-space masks retain their existing grayscale conversion. Alpha masks do
not depend on the group's color encoding.

## Inverse transforms and limits

ICC curve inversion follows ICC.1:2022 Annex F.1. Sampled curves must be
nonconstant and monotone, either increasing or decreasing. A flat interval uses
its highest input, except a flat interval ending at input 1 uses its lowest.
Values outside the reachable output range use the nearest reachable value.
Parametric curves use analytic inverses for their power and linear pieces,
including clipping and flat intervals. A selected A2B table is never bypassed in
favor of otherwise usable matrix/TRC tags.

Prepared inverses borrow the validated profile data and are cached by rendering
intent. Preparation and profile-equivalence checks consume the renderer's work
budget. Matrices use pivoted inversion and are rejected when their coefficient,
inverse-coefficient or estimated condition bounds exceed 1e30, 1e64 or 1e12,
respectively. Matrix residuals and analytic curve inverse residuals must be no
larger than 1e-9. An inverse that underflows to an incorrect color fails instead
of returning a plausible but wrong black pixel.

These bounds are implementation limits, not additional PDF or ICC requirements.
They do not establish full color-management conformance. Multiprocess elements,
nonzero calibrated black points, overprint and
spot-aware blending still need work.

## Tests

`make test-pdf-cie-groups` runs 295,604 inverse-curve and matrix checks, 212 page
checks and 1,736 allocation-failure checks. Page cases cover native-component
blending, isolated/non-isolated forms, source images, Type 3 glyphs, text,
pattern-cache changes, wide-gamut transport, soft-mask luminosity, intent state
and rejected inverse transforms. Paired native compositions require exact
pixels; analytic color conversions allow one byte of floating-point rounding.

The owned `assets/pdf/cie-blending.pdf` fixture shows device and ICC blending,
a wide-gamut source and a luminosity mask. Its complete raster remains
**decoded-unverified**. Rebuild it with
`scripts/generate_pdf_cie_blend_fixture.py`.

References: ISO 32000-1 sections 11.3.4, 11.6.5 and 11.7.5.3, and
[ICC.1:2022 Annex F](https://www.color.org/specification/ICC.1-2022-05.pdf).
See [PDF colors](pdf-colors.md) for source models and
[PDF transparency](pdf-transparency.md) for group composition.

A focused two-document corpus comparison now renders `bigboundingbox.pdf` and
`cid_cff.pdf`, which previously failed at ICC page-group admission. At 96 DPI with
all five optional text/hinting/graphics profiles enabled, mean absolute channel
errors were 2.52905 and 0.06397, respectively. Both still differ from the reference
raster. The larger document also required exact canvas cropping of an oversized
axis-aligned Form boundary. Neither result establishes complete document fidelity.


## Lookup-based destination transforms

Destination selection uses B2A1 for relative or supported absolute colorimetry,
B2A0 for perceptual intent and B2A2 for saturation. A missing selected table falls
back to B2A0. Only when neither exists may an invertible matrix/TRC model supply
the reverse transform. A malformed selected table fails; unused malformed intent
tables do not disable a different valid intent. Unsupported B2D elements reject
destination use without disabling an otherwise supported forward source profile.

The native table engine supports `mft1`, `mft2` and version 4 `mBA ` destinations.
Legacy tables apply the permitted PCSXYZ matrix before the input tables, CLUT
and output tables. `mBA ` applies B curves, matrix, M curves, CLUT and A curves in
that order, using only the combinations permitted by ICC.1:2022 section 10.13.
Curve elements may share storage. CLUT grids may differ by axis. Stage extents,
reserved fields, channel counts and nonfinite inputs are checked before use.

PCS input encoding distinguishes XYZ, modern Lab and legacy 16-bit Lab. Eight-bit
PCSXYZ is undefined by ICC.1 and remains rejected. Perceptual conversion restores
the reference-medium black encoding before the destination table; saturation
uses that encoding when `rig2` selects the perceptual reference medium. Supported
absolute conversion removes the media-white scaling before table evaluation.

The table core supports one, three and four output channels, but the PDF group
compositor still admits only gray and RGB profiles. Four-channel kernel tests are
not evidence of CMYK group support. This also does not add a monitor-profile API,
black-point compensation or arbitrary output intents.

`make test-pdf-icc-reverse` adds 10,700 raw pipeline checks, 94 page checks and
435 allocation-failure checks. Cases cover stage order, table priority, intent
selection, XYZ/Lab encodings, soft-mask luminosity, in-place evaluation, output
bounds and every truncation of selected table fixtures. The optional independent
pipeline comparison covers 348 samples:

    python3 scripts/test_pdf_icc_reverse.py bin/test_pdf_icc_reverse --reference

`assets/pdf/icc-destination-blending.pdf` contains authored lookup transforms,
not a physical device characterization. Its full raster is decoded-unverified.
Rebuild it with `scripts/generate_pdf_icc_destination_fixture.py`.
