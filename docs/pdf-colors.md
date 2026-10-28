# PDF color conversion

PDF source colors are converted before transparency compositing. DeviceGray,
DeviceRGB and DeviceCMYK keep their existing unmanaged conversion rules.
CalGray and CalRGB now have a separate calibrated-source path; selecting one
must not silently treat its samples as ordinary device values.

## Calibrated gray and RGB sources

`pdf_calibrated.c` implements the CalGray and CalRGB equations in ISO 32000-1,
sections 8.6.5.2 and 8.6.5.3. Inputs are clipped to [0, 1] before gamma decoding.
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
CalGray/CalRGB as a group's own blending space remain unsupported; they are
rejected rather than being mistaken for DeviceRGB or DeviceGray.

## Tests and remaining work

`make test-pdf-calibrated` covers analytic sRGB samples, gamma and matrix defaults,
white adaptation, indirect/compressed objects, graphics-state restoration, text,
images, patterns and all four mesh families. Allocation-failure tests check cleanup
and compare recovered renders with the baseline.

Optional independent comparisons allow one channel value of rounding difference:

    python3 scripts/test_pdf_calibrated.py bin/test_pdf_pages bin/test_pdf_clip_oom --reference

Out-of-range CalRGB inputs with fractional gamma use the specification's clipped
input values as their oracle. The comparison renderer returns a different result
for that case, so it is not used as the expected result there.

Lab, ICCBased profiles, output intents, nonzero black points, calibrated blending
spaces, general rendering-intent selection, Separation/DeviceN and overprint
remain unfinished. This source-color support does not establish complete color
management or visual parity for arbitrary PDFs.
