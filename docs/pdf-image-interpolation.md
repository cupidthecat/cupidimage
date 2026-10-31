# PDF image interpolation

Image XObjects and inline images honor `Interpolate true`. The default remains
nearest-sample painting. There is no CLI override: the image dictionary selects
the profile, including images drawn inside Type 3 glyphs, Forms and patterns.
Inline images may use the standard `I` abbreviation. Indirect booleans are resolved;
null or absent entries retain the default, and a non-boolean value is rejected.

## Sampling profile

The current interpolator is bilinear. Samples lie at the centers of their cells,
with the nearest edge sample extended to the image boundary. Device pixel centers
are mapped through the inverse image transform into the image's unit square.
A sample uses at most four neighboring source pixels. One-pixel-wide or high
images reduce to one-dimensional interpolation, and a single-pixel image stays
constant. Sampling does not extend paint beyond the image's transformed boundary.

Packed 1-, 2- and 4-bit values, 8-bit values and big-endian 16-bit values use the
same path. Decode and component-range clipping precede interpolation. Indexed
images expand their palette entries first: interpolating palette indices would
invent colors unrelated to the neighboring pixels. Color-space conversion and
compositing follow the filtered components.

Color-key masks are tested on original sample values. Their coverage is filtered
with associated color components so a hidden key color cannot bleed into visible
neighbors. Stencil images filter paint coverage and retain the caller's color or
pattern.

Explicit and soft image masks have their own `Interpolate` flag and sample grid.
Their unit squares coincide with the parent image, even when dimensions differ.
A mask's flag does not enable interpolation for the parent, or vice versa.
Explicit-mask coverage affects shape; soft-mask alpha follows the existing
alpha-is-shape setting.

For a soft mask with Matte, source colors are recovered in the image color space.
The filter carries recovered components with their source alpha, then divides by
the filtered alpha. Fully transparent samples do not contribute an undefined
color. The independently sampled soft mask still supplies final image alpha.
The existing equal-dimension requirement for Matte remains in force.

## Bounds and ownership

The coordinate sampler allocates no storage and validates dimensions, finite
coordinates and its mode before integer conversion. Packed sample buffers retain
the 64 MiB decode bound. Interpolated painting charges the shared page work budget
before traversing its cropped output rectangle, including mask and component work.
Allocation failure and exhausted work fail the image rather than returning a
successful partial result.

## Tests and remaining work

`make test-pdf-image-interpolation` checks sample-center placement, border extension,
single-pixel axes, invalid inputs and large dimensions in the native sampler.
The focused suite currently has 69 page checks and 301 allocation checks.
Page tests cover affine placement, single-pixel axes, Decode, palette expansion, packed samples, color-key coverage,
stencils, independent mask flags, Matte recovery, inline abbreviations and Type 3
image execution. Knockout cases distinguish explicit-mask shape from soft-mask
opacity with both alpha-is-shape settings. Successful and rejected metadata cases also run through
allocation-failure injection.

The existing image, mask, inline-image and metadata suites continue to exercise
the default nearest-sample path. Their exact results are separate from an
interpolated full-page comparison.

Bilinear filtering is not a complete resampling solution. Area filtering for
minification, more extensive affine/color-space qualification and antialiased
image boundaries remain unfinished. Different rasterizers may choose different
filters, sample anchors and intermediate precision. An independent 2-by-2 RGB
enlargement comparison still differs substantially with this profile. Honoring
the flag does not establish pixel parity.

`assets/pdf/image-interpolation.pdf` shows nearest and filtered color samples, a
Type 3 stencil and an independently filtered soft mask. The full page remains
decoded-unverified. Regenerate it with
`scripts/generate_pdf_image_interpolation_fixture.py`.

ISO 32000-1 section 8.9.5.3 defines the flag and explicitly leaves the interpolation
algorithm to the reader. Table 89 defines its type and default; section 8.9.7
lists inline-image abbreviations. Image masks and Matte follow sections 8.9.6 and
11.6.5.3. See the [standards guide](pdf-standards.md) for the complete specifications.
