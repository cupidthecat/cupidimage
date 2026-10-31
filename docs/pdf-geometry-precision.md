# PDF geometry precision

The PDF renderer keeps geometry in double precision from parsed operands through
transform composition, page origins, paths and stroke construction. Earlier
versions narrowed several of those values to floats before rasterization.

That narrowing could change text placement even when two operator sequences
represented the same transform. For example, translating by +16777216, then +1,
then -16777216 did not preserve the same position as a direct +1 translation.
The owned regression checks that case without changing the font, glyph or PDF
advance.

## Shared geometry

The precision change covers:

- Current transformation matrices and saved/restored graphics state.
- Page origins, path endpoints and curve controls, and line widths.
- TrueType, CFF, Type 1, Type 3 and bitmap-fallback placement.
- Form matrices, cached pattern transforms, masks and annotation appearances.
- Image corners, shading and mesh/patch geometry that uses the same CTM.

Shared affine operations live in `src/formats/pdf/geometry`. Matrices retain the
PDF multiplication order and support in-place composition. Font fitting still
uses its specified 16.16 and 26.6 conversions. Pixel coverage remains discrete;
using doubles does not turn raster output into exact real arithmetic.

The existing finite-value, path, coordinate, memory, work and recursion limits
still apply. Inverse determinants, coefficients and sampled coordinates are
checked before clipping or integer pixel lookup. Finite CTM entries can still
produce a nonrepresentable inverse; when needed for rendering, that fails the load
instead of sampling with a NaN. Singular finite transforms retain their empty-paint behavior. The lower-range checks for UserUnit and pattern scale remain explicit
validation rules, not coordinate-storage conversions. No image format outside
PDF changes its geometry representation.

## Binary scan conversion

Binary scanlines now retain double-precision intersections. The displacement is
multiplied before division, avoiding an extra rounding step that could move an
exact pixel-center crossing across its endpoint comparison. If the product
would overflow, the calculation scales first. Nonfinite results still fail.
The horizontal-edge threshold and the winding/endpoint rule are unchanged.

The scanline tests use an exact integer orientation oracle for eighth-unit
coordinates rather than copying the renderer's floating-point division. Both
the previous float calculation and a direct double translation of it disagree
with that oracle on boundary cases. The revised calculation passes those cases.

## Verification

`make test-pdf-geometry-precision` checks matrix composition, page-origin
subtraction and equivalent complete-page transforms. It covers all text modes,
all four outline/font-program paths, Forms, patterns, masks, images, strokes,
shading and annotation appearances. Allocation-failure tests cover the nested
paths. A deliberately oversized stroke still hits the existing coverage limit.

The optional `--reference` pass checks selected equivalent-page controls with an
independent renderer. A fractional-origin case is reported separately when that
renderer quantizes the two expressions to different glyph phases. That difference
is not counted as agreement or used to weaken the native equality check.

The sample `assets/pdf/geometry-precision.pdf` places equivalent text transforms
on two rows. Their glyphs should have the same X positions. Rebuild it with
`scripts/generate_pdf_geometry_precision_fixture.py`. Its whole raster remains
decoded-unverified; the focused controls provide the evidence described above.

The coordinate and text-matrix rules follow ISO 32000-1 sections 8.3 and 9.4.
These tests establish selected numerical behavior, not complete PDF rendering
fidelity. Font substitution, raster policies and the other limits in the
[font guide](pdf-fonts.md) remain separate work.
