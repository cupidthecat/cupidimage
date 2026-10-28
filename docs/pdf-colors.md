# PDF color conversion

PDF source colors are converted before transparency compositing. DeviceGray,
DeviceRGB and DeviceCMYK use their existing unmanaged conversion rules unless
the current resources replace them with a supported default color space.
CalGray, CalRGB and Lab have a separate calibrated-source path; selecting one
must not silently treat its samples as ordinary device values.

## Calibrated sources

`pdf_calibrated.c` implements the CalGray, CalRGB and Lab equations in ISO 32000-1,
sections 8.6.5.2 through 8.6.5.4. Inputs are clipped to [0, 1] before gamma decoding.
CalGray multiplies the decoded gray by the source white point. CalRGB applies
its gamma vector and column-ordered matrix to obtain CIE XYZ values.

The output target is sRGB with a D65 white point. The source white is adapted
with the linearized Bradford transform from
[ICC.1:2022, Annex E.3](https://www.color.org/specification/ICC.1-2022-05.pdf).
XYZ values are converted to linear RGB, clipped to the output gamut, and encoded
with the sRGB transfer function. This is a fixed output target, not a complete
ICC profile engine or implementation of every rendering intent.

WhitePoint is required. Gamma and Matrix use their specified defaults, including
when optional entries are null. Arrays, dictionaries and their numeric values
can be indirect. Invalid types, lengths, white points and gamma values fail.
The current path supports zero BlackPoint only. Nonzero black-point mapping
requires the unfinished black-compensation/output-intent pipeline and is rejected.

## Lab components and samples

Lab keeps L* in [0, 100] and a*/b* in the declared Range, whose default is
[-100, 100, -100, 100]. Values are clipped to their respective ranges, not to
[0, 1]. The piecewise Lab-to-XYZ transform uses the source white point before
adaptation to the output target. Range can be indirect; null retains its default.

Image samples without an explicit Decode array map into those component ranges.
Indexed palette bytes also map into the base space's ranges, so signed Lab values
survive palette lookup, pattern selection and mesh interpolation. Explicit Decode
arrays still take precedence. Matte unblending stays in the parent's source space.
For Indexed images it applies to the palette's base components, as clarified in
ISO 32000-2:2020 Table 144, rather than treating a palette index as a color.

## Shared painting path

Calibrated sources work with fill and stroke colors, native text, image XObjects,
inline images, Indexed palettes, tiling patterns and supported shadings/meshes.
`q`/`Q` preserves the complete source space. Device-color operators reset the
calibrated selection, subject to default-space remapping. Deferred uncolored patterns retain their base color space,
not just a component count.

Named inline-image color spaces retain their array structure and object-reference
context when resolved. This includes calibration dictionaries stored in compressed
objects. Palette lookup, image Decode and matte removal happen in source space
before the color conversion. Shading interpolation likewise stays in source space.

RGB and gray transparency groups accept these colors as painted sources.
CalGray/CalRGB/Lab as a group's own blending space remain unsupported; they are
rejected rather than being mistaken for DeviceRGB or DeviceGray.

## Tests and remaining work

`make test-pdf-calibrated` covers analytic sRGB samples, gamma and matrix defaults,
white adaptation, indirect/compressed objects, graphics-state restoration, text,
images, patterns and all four mesh families. Allocation-failure tests check cleanup
and compare recovered renders with the baseline.

`make test-pdf-lab` adds analytic Lab/sRGB pairs, component clipping, restricted
ranges, image Decode defaults, signed matte values, Indexed base components,
text, meshes, patterns and allocation-failure recovery.

Optional independent comparisons allow one channel value of rounding difference:

    python3 scripts/test_pdf_calibrated.py bin/test_pdf_pages bin/test_pdf_clip_oom --reference
    python3 scripts/test_pdf_lab.py bin/test_pdf_pages bin/test_pdf_clip_oom --reference

Out-of-range CalRGB inputs with fractional gamma use the specification's clipped
input values as their oracle. The comparison renderer returns a different result
for that case, so it is not used as the expected result there.

Complete ICC profile handling, output intents, nonzero black points, calibrated
blending spaces, complete rendering-intent handling, full NChannel support and
overprint remain unfinished. This source-color support does not establish complete color
management or visual parity for arbitrary PDFs.

## ICC matrix, curve and lookup profiles

ICCBased source spaces now decode supported version 2 and version 4 profiles
instead of using their alternate device space. The current profile models are:

- Three-component RGB input/display profiles with XYZ matrix columns and
  per-channel tone-reproduction curves.
- One-component gray input/display/output profiles with a gray tone curve.
  Their profile connection space may be XYZ or Lab.
- Gray, RGB and CMYK forward lookup profiles using 8-bit Lab or 16-bit XYZ/Lab
  tables, and version 4 A-to-B curve/matrix/CLUT stages. Reverse tables and
  multiprocess models remain unsupported.

The parser reads big-endian tag directories and signed fixed-point values.
Identity curves, u8Fixed8 gamma curves, sampled 16-bit curves with linear
interpolation, and parametric curve functions 0 through 4 are supported. Curve
outputs are clipped to their specified [0, 1] range. Undefined/nonfinite curve
results fail instead of entering the compositor. Source curves are evaluated
forward; no inverse transform is inferred from a noninvertible curve.

Transforms use the D50 profile connection space and then the fixed sRGB output
conversion described above. The profile's header rendering intent is not used
as a PDF graphics-state rendering intent. PDF intent state selects A2B0 for
perceptual, A2B1 for relative colorimetry, or A2B2 for saturation. Missing selected
tables fall back to A2B0, then matrix/curve tags. Unsupported selected A2B types
and profiles containing D2B transforms are rejected rather than silently replaced
with matrix tags. A malformed transform for an unused intent does not prevent
using another supported transform. Output profiles, black-point compensation and
ICC blending spaces remain unfinished.

PDF N must match the supported profile's channel count. Range may be absent,
null or an indirect array, but this implementation currently requires [0, 1]
for each component. Profile references and metadata can pass through indirect
or compressed objects. PDF N may be 1, 3 or 4 and must agree with the profile.
Unsupported profile models, unusual component ranges and fallback-to-Alternate
policy remain open work.

Decoded profile data are immutable and shared across saved graphics states,
Forms, patterns and repeated selections within a page. The page owns the cache
and releases it on success or failure. Limits are 16 MiB per decoded profile,
32 MiB for the page cache, 256 cached profile resources, 4,096 directory entries
and 65,536 samples per tone curve. Tag offsets, lengths, alignment, required
rendering tags and shared curve storage are checked before use. This is bounded
rendering support, not a complete ICC conformance validator.

`make test-pdf-icc` generates owned profiles and compares them with calibrated
source-color oracles. It covers all curve functions, XYZ/Lab gray profiles,
state restoration, images, Indexed palettes, nested Forms, compressed metadata,
reference cycles, malformed bounds, cache reuse and allocation failures.
Optional independent comparisons allow two channel values of rounding:

    python3 scripts/test_pdf_icc.py bin/test_pdf_pages bin/test_pdf_clip_oom --reference

The profile model and curve equations follow ISO 32000-1 section 8.6.5.5 and
ICC.1:2022 sections 10.6/10.18 and Annex F. Support for one model does not establish
full ICC handling or arbitrary-document visual fidelity.

## Default spaces and initial pattern colors

DefaultGray, DefaultRGB and DefaultCMYK in the current ColorSpace resources
replace a selected device space once, as specified by ISO 32000-1 section
8.6.5.6. The replacement must have the same component count and cannot be Lab,
Indexed or Pattern. Supported calibrated and ICC source spaces work here;
unsupported replacement models still fail. A device-space replacement is not
remapped again. Null or undefined defaults leave the original device space intact.

Remapping applies to color operators, image XObjects, inline images, Indexed
bases, uncolored pattern bases and shadings. Forms use their own resources for
new selections while preserving inherited graphics-state colors. Soft-mask
samples remain alpha values rather than being converted through DefaultGray.
A transparency group with no explicit CS inherits its parent blending space;
unused default spaces do not cause it to fail. Explicit group spaces are still
subject to the blending-space restrictions above.

Selecting Pattern without choosing a pattern with SCN/scn paints nothing
(ISO 32000-1 Table 74). This applies to paths and text, including combined fill
and stroke. An empty stroke does not cover an otherwise visible fill. Path and
text clipping still take effect even when the painting color is empty.

`make test-pdf-default-colors` checks resource scope, default references,
replacement validation, compressed resources, image and pattern bases, native and fallback text,
clipping, combined painting and allocation recovery. Failed resource lookups
are distinguished from absent defaults, so allocation failures cannot silently
change the output colors. Optional comparisons use:

    python3 scripts/test_pdf_default_colors.py bin/test_pdf_pages bin/test_pdf_clip_oom --reference

## Separation tint transforms

Single-colorant Separation sources use their alternate device, calibrated or
supported ICC space for the fixed additive output target. The tint is clipped to
[0, 1] and passed to the supplied function before alternate-space conversion.
Function types 0, 2, 3 and 4 use the existing bounded function evaluator. The
initial tint is 1. PDF names and function references can be indirect, and function
streams retain the cross-reference context needed for compressed metadata.

The same source path serves text, fill/stroke, image XObjects, inline images,
Indexed palettes, uncolored pattern bases, gradients and meshes. Saved graphics
states retain the selected transform. New selections in a Form use that Form's
resources; cached transforms distinguish different resolved alternates. Default
spaces apply to ordinary Separation alternates, while a default-space replacement
is not recursively remapped.

None contributes no paint or shape, but does not suppress path or text clipping.
Combined fill/stroke keeps the visible channel when the other uses None. All
ignores its alternate and applies the complemented tint to every additive channel,
as specified explicitly in ISO 32000-2:2020 section 8.6.6.4. The ignored alternate
and function operands still need the appropriate object kinds. Their unused
profile data and function programs are not executed.

All has a deliberate specification-based oracle: some comparison rasters use its
alternate tint transform instead. The owned All asset therefore checks the
specified inverse-gray result rather than treating that reference behavior as
correct. This difference remains relevant to visual comparison.

Each page owns an immutable transform cache, limited to 256 definitions, 1 MiB
per definition key and a 64 MiB budget charging keys, function data and compiled
programs. Repeated identical definitions with the same resolved alternate reuse
entries. Tint evaluation uses
the shared shading-work budget. Cache storage is released after patterns and
before ICC profiles on both success and failure.

`make test-pdf-separation` covers tint clipping, initial colors, all four function
types, images, text, patterns, meshes, resource scope, compressed metadata,
allocation recovery and cache limits. Three owned assets have analytic references.
The optional independent pass is:

    python3 scripts/test_pdf_separation.py bin/test_pdf_pages bin/test_pdf_clip_oom --reference

This is alternate-space rendering for an additive target. NChannel spot mixtures,
physical spot planes, overprint simulation and spot-aware transparency blending
remain unfinished. Existing ICC and function limits also apply to tint transforms.

## DeviceN tint transforms

Legacy DeviceN sources support 1 to 32 components through the same alternate-space
pipeline as Separation. Component names must be unique except for None; All is
not permitted. Initial component values are all 1. Mixed None components still
reach the tint function, while a source containing only None contributes no paint
or shape. Clipping remains effective.

Text, paths, packed and byte-sampled images, inline images, masks, Indexed palettes,
uncolored patterns and shadings retain all source components until conversion.
Image Decode, color-key masks and soft-mask Matte values use the source component
count. This does not expand the component counts supported by JPEG or other image
compression formats.

Sampled and calculator functions accept up to 32 inputs and outputs. Exponential
and stitching functions still take one input. Sampled interpolation skips axes
whose two endpoints coincide, then charges the shared work budget for the active
corners and output components before reading samples. Cubic sampled interpolation
(Order 3) remains unsupported.

Indexed lookup data is immutable and owned by the page. Identical byte sequences
share storage without sharing their base-space interpretation. A page allows up
to 256 distinct palettes, 8,192 bytes per palette and 16 MiB of charged storage.
This keeps saved graphics states small even with 32-component bases.

Optional legacy DeviceN attribute dictionaries are accepted. Process-only NChannel
spaces use the path described below. NChannel spot mixtures, physical spot planes,
overprint and spot-aware blending are still missing. Supporting an alternate tint
transform alone is not full NChannel support.

`make test-pdf-devicen` exercises component counts through 32, sampled and
calculator transforms, images with TIFF predictors, masks, text, patterns, meshes,
name validation, cache limits and allocation failures. Three owned assets cover
32-component image/palette data and Indexed Lab patch ranges. Independent checks
can be requested with:

    python3 scripts/test_pdf_devicen.py bin/test_pdf_pages bin/test_pdf_clip_oom --reference

The installed comparison renderer rejects sampled functions above 16 inputs.
Those 32-input cases use explicit RGB and analytic oracles instead; they are not
counted as independent comparisons.

## Process-only NChannel spaces

NChannel spaces containing only process components use their Process dictionary
rather than the aggregate alternate tint function. Supported process spaces are
DeviceGray, DeviceRGB, DeviceCMYK, CalGray, CalRGB and the supported ICC
models. Lab, special spaces and unknown spot components are rejected.

Non-CMYK spaces require every process component in its normal order. CMYK may
select a subset in any order; omitted components have zero tint. The reserved
Cyan, Magenta, Yellow and Black names retain their CMYK meanings, including when
the Process Components array uses custom names. Duplicate mappings and None are
not valid NChannel components. All remains prohibited for every DeviceN space.
RGB/gray process values are additive, while CMYK values are subtractive. Initial
values remain 1 for every selected component, as specified for DeviceN.

Process definitions and component names may be indirect. Definitions for process
components in Colorants are ignored. The shared tint cache distinguishes the
resolved process space and component mapping, including scoped default colors.
Text, paths, images, masks, Indexed palettes, patterns and shadings use the same
process conversion path.

The aggregate alternate and tint function are still required to have suitable
object kinds. Their unused profiles and function bodies are not executed for a
supported process-only definition. An authored conflict test deliberately supplies
a black aggregate function for RGB process values [.25, .5, .75]. Native output
uses the process color; the installed comparison renderer uses the black fallback.
That case follows ISO 32000-2 section 8.6.6.5 and is a specification-based check,
not an independent raster match. Consistent alternate definitions are compared
separately.

`make test-pdf-nchannel` checks all CMYK subset permutations, natural RGB/gray
values, calibrated/ICC process spaces, indirect metadata, scoped defaults, cache
limits and allocation failures. Three owned assets have consistent alternates and
analytic references. The RGB and gray assets also match the installed comparison
raster exactly. The CMYK subset asset follows the existing native device conversion:
its green is [0, 255, 0], while the comparison raster is [0, 166, 80]. That asset is
a component-mapping regression, not a cross-renderer color-management pass.
The optional independent pass is:

    python3 scripts/test_pdf_nchannel.py bin/test_pdf_pages bin/test_pdf_clip_oom --reference

Spot-only and mixed process/spot NChannel spaces remain unsupported, even when
Colorants entries are present. MixingHints, physical ink interactions, overprint
and spot-aware transparency require further work. Existing ICC and color-management
limits still apply; this is not complete NChannel support.

## ICC lookup-table details

`pdf_icc_lut.c` evaluates the forward input tables, multidimensional CLUT and
output tables. One-dimensional tables use linear interpolation. Three-component
CLUTs use tetrahedral interpolation; four-component CLUTs interpolate between two
three-dimensional slices. Device-input matrices must be identity matrices.
Sixteen-bit tables have 2 to 4,096 entries per channel; eight-bit tables have 256.
All payload extents must fit the validated profile data. Producer padding after
the declared tables is ignored, without treating it as extra samples or stages.

PCSXYZ output is decoded without prematurely clipping values above 1. Legacy
16-bit Lab encoding is preserved even in version 4 profiles. Eight-bit PCSXYZ is
not supported because its interpretation is undefined by ICC.1. The table layouts,
encodings and tag selection follow ICC.1:2022 sections 8.10, 10.10 and 10.11.

The existing profile cache and memory limits apply. CMYK profiles flow through
text, packed/JPEG images, soft-mask Matte processing, Indexed palettes, patterns,
shading, default-space replacement and supported NChannel process definitions.
This does not change the unmanaged conversion used by plain DeviceCMYK.

`make test-pdf-icc-lut` exercises raw table interpolation, extrema, nonfinite input,
malformed extents, source integration and allocation recovery. Its optional
independent pass compares table evaluations and rendered pixels:

    python3 scripts/test_pdf_icc_lut.py bin/test_pdf_icc_lut bin/test_pdf_pages bin/test_pdf_clip_oom --reference

Three owned assets cover CMYK/Lab, eight-bit RGB/Lab and XYZ values above 1.
Their raster references are analytic. Existing installed profiles can also be
checked without copying them into the repository:

    python3 scripts/compare_pdf_icc_profiles.py bin/test_pdf_pages path/to/profile.icc

That optional comparison uses relative colorimetry with black-point compensation
disabled and permits a two-channel-value rounding difference. Forty samples each
from two installed CMYK profiles matched exactly in the current check. This is
not a claim about every profile or every input color.

Black-point compensation remains a known difference. An authored constant A2B1
profile produces gray 119 without compensation; the comparison renderer maps its
nonzero source black point to black. A separate uncompensated color-engine check
confirmed the native result. The compensated result is not used as an oracle for
the current uncompensated relative-colorimetric path. Black-point compensation,
output profiles, reverse tables, multiprocess tags and ICC blending spaces still require
work. The intent support below does not resolve those differences.


## Rendering intent state

The `ri` operator, ExtGState `RI` and image `Intent` entries select the intent at
painting time. The default is RelativeColorimetric. Unknown names, including the
empty name, also select RelativeColorimetric as required by ISO 32000-1 section
8.6.5.8. Null or absent dictionary entries leave the inherited intent unchanged;
wrong value types fail. Dictionary entries may be indirect.

Intent survives `q`/`Q`, follows Form inheritance, and remains local when an image
overrides it. Inline images use the same conversion path. Tiling and shading
patterns start with the defining content stream's initial state, rather than the
state at pattern selection. A shading pattern's ExtGState can override that
initial intent. Cached shading patterns distinguish those initial states.
Uncolored pattern bases are converted using the caller's paint state.

Each cached ICC profile retains separate validated transforms. This lets a color
space be selected before an intent changes without retaining the old conversion.
Version 4 absolute colorimetry uses the relative transform and media-white scaling
from ICC.1:2022 section 6.3.2.2. It requires a valid positive `wtpt` tag. Version 2
absolute colorimetry remains unsupported because its legacy white-point and
adaptation conventions need separate handling.

Version 4 perceptual LUTs map the standardized reference-medium black point to
the fixed output's zero black point, using the inverse linear XYZ equations in
ICC.1:2022 section 6.3.4.3. Saturation LUTs use that mapping only when `rig2`
explicitly identifies the perceptual reference medium gamut. Without that tag,
the reference gamut is unspecified, so the renderer does not assume it. This is
reference-medium encoding, not general source black-point detection or compensation.
Version 2 perceptual profiles can still differ from renderers that automatically
compensate their source black point. Saturation profiles with unspecified reference
gamuts can also differ from implementations that assume the perceptual medium.

`make test-pdf-intents` checks state changes, tag selection, fallback, malformed
metadata, text, strokes, images, inline images, patterns, Forms and allocation
recovery. Its optional `--reference` pass compares all four supported version 4
intent paths with an independent color engine, with optional compensation off.
Two analytic assets cover distinct intent tables and local image overrides.
Regenerate them with `scripts/generate_pdf_intent_fixtures.py`.

This does not complete intent handling for calibrated spaces, transparency-group
conversion, document/page output intents, version 2 absolute colorimetry or
arbitrary source/destination profiles.

[Issue #51](https://github.com/cupidthecat/cupidimage/issues/51) tracks the remaining
color-management and ink-rendering requirements. The feature matrix keeps their
status separate from these focused implementation checkpoints.


## Version 4 A-to-B stages

The `mAB ` transform model follows ICC.1:2022 section 10.12. Supported combinations
are B curves alone; M curves, matrix and B curves; A curves, CLUT and B curves; or
all five stages. They execute in A → CLUT → M → matrix → B order. Input channels
may be gray, RGB or CMYK; the PCS has three output components. Different input
and output channel counts require a CLUT.

Curves use the shared sampled/gamma/parametric evaluator. The CLUT supports separate
grid sizes for each dimension and 8-bit or 16-bit samples. Its channel order and
interpolation match the older table path. Matrix offsets are applied after the
three-by-three product; results are clipped before the B curves. Shared curve
elements and reordered stage storage are supported. Reserved fields, stage
combinations, alignment, padding and payload extents are checked. Matrix and CLUT
storage cannot overlap another stage. Data remain borrowed from the immutable
profile cache, with no per-pixel allocation.

These profiles use the full-range version 4 PCS Lab encoding, not the older
lut16 Lab scale. PCSXYZ values above 1 remain available to the output conversion.
Version 2 profiles containing mAB tags are rejected. The existing per-intent source
selection applies to the new stages; reverse mBA tables and multiprocess elements
remain unfinished.

`make test-pdf-icc-mab` checks stage order, each permitted combination, curve
sharing, nonuniform grids, malformed offsets, clipping and PDF source integration.
The optional `--reference` pass checks raw pipelines and rendered samples against
an independent color engine. Matrix-overflow cases use the specification's explicit
clipping rule as their oracle: the independent raw-pipeline API can return values
above 1 for those cases, so they are not counted as independent matches. Three
analytic assets cover B-only curves, a CMYK CLUT and matrix offsets.

    python3 scripts/test_pdf_icc_mab.py bin/test_pdf_icc_mab bin/test_pdf_pages bin/test_pdf_clip_oom --reference

Regenerate these assets with `scripts/generate_pdf_icc_mab_fixtures.py`.
