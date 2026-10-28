# PDF color conversion

PDF source colors are converted before transparency compositing. DeviceGray,
DeviceRGB and DeviceCMYK keep their existing unmanaged conversion rules.
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
calibrated selection. Deferred uncolored patterns retain their base color space,
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
blending spaces, general rendering-intent selection, Separation/DeviceN and
overprint remain unfinished. This source-color support does not establish complete color
management or visual parity for arbitrary PDFs.

## ICC matrix and tone-curve profiles

ICCBased source spaces now decode supported version 2 and version 4 profiles
instead of using their alternate device space. The current profile models are:

- Three-component RGB input/display profiles with XYZ matrix columns and
  per-channel tone-reproduction curves.
- One-component gray input/display/output profiles with a gray tone curve.
  Their profile connection space may be XYZ or Lab.

The parser reads big-endian tag directories and signed fixed-point values.
Identity curves, u8Fixed8 gamma curves, sampled 16-bit curves with linear
interpolation, and parametric curve functions 0 through 4 are supported. Curve
outputs are clipped to their specified [0, 1] range. Undefined/nonfinite curve
results fail instead of entering the compositor. Source curves are evaluated
forward; no inverse transform is inferred from a noninvertible curve.

Transforms use the D50 profile connection space and then the fixed sRGB output
conversion described above. The profile's header rendering intent is not used
as a PDF graphics-state rendering intent. General intent selection, output
profiles, black-point compensation and ICC blending spaces remain unfinished.
Profiles containing higher-priority A2B or D2B transforms are rejected rather
than silently replacing those LUT transforms with matrix tags.

PDF N must match the supported profile's channel count. Range may be absent,
null or an indirect array, but this implementation currently requires [0, 1]
for each component. Profile references and metadata can pass through indirect
or compressed objects. Unsupported profile models, CMYK/LUT profiles, unusual
component ranges and fallback-to-Alternate policy remain open work.

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
