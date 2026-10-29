# PDF glyph coverage

The internal coverage converter accepts device-space line, quadratic and cubic
outlines and returns one alpha byte per pixel. It supports nonzero and even-odd
fill rules, fractional coordinates and clipping to the output rectangle.

ISO 32000-1 sections 10.6.1 and 10.6.4 describe scan conversion but do not
prescribe a single glyph raster algorithm. Independent bitmap checks therefore
complement the PDF semantics tests.

It is not connected to PDF page painting yet. Page text still uses center-point
coverage, and the native TrueType instruction path still needs page integration.
This module does not establish complete font rendering or visual parity.

## Coordinates and output

Input coordinates use signed 26.6 fixed point. Rows increase with y, so the
caller supplies coordinates in its output orientation. Each point carries an
on-curve, quadratic-control or cubic-control tag and a contour-end flag.

The converter calculates signed areas for straight segments. It subdivides
curves using a device-space error criterion before accumulating their areas.
It does not sample a coarse grid of points inside each pixel. Curve coverage is
still finite-precision, not exact mathematical integration.

Output quantization retains the scan converter's orientation-dependent rounding.
For a positive covered area, reversing a contour can change a boundary pixel by
one alpha level. Fractional rectangle tests check the signed quantization rule
explicitly; they do not silently tolerate arbitrary pixel differences.

The implementation is adapted from the licensed source documented in
[the third-party notice](../third_party/freetype-raster/README.md). It is compiled
into the C99 library and has no external font-library runtime dependency.

## Failure and resource handling

The call accepts at most 65,535 points, dimensions up to 32,767 pixels, output
up to 16 MiB and coordinates within +/-32,768 pixels. It rejects invalid tags,
unterminated contours and invalid dimensions before scan conversion. The outline
walker checks curve sequencing.

A caller-owned work budget pays for input validation, output initialization,
outline callbacks, curve subdivision, cell lookup, rows and band retries. Every
call has its own state, cancellation buffer and cell pool. Failure frees the
partial image and returns a null output. No partial spans reach a page surface.

Accumulated cell areas use 64-bit integers. The tests include 20,000 overlapping
triangles, whose area accumulation can exceed a 32-bit cell's range.

## Checks

Run `make test-pdf-coverage` for:

- 335,454 analytic rectangle, overlap, limit and work-budget checks;
- 1,666 allocation-failure and cancellation cleanup checks;
- 250 deterministic extreme-coordinate and mixed-tag inputs;
- an optional independent comparison of 600 authored line, quadratic and cubic
  outlines in both fill rules, when `freetype-py` is installed;
- 144 optional end-to-end native glyph bitmaps, covering simple and composite
  instruction execution, nonuniform sizes, rotations, reflections, shears and
  fractional translations.

The independent cases match every output byte with engines 2.13.2 and 2.13.3.
The authored-outline cases isolate scan conversion. The 144 glyph cases also
exercise the legacy instruction profile and transformed-outline adapter. Neither
set tests PDF font selection, page text placement, clipping composition, stroke
coverage or page pixels.

Remaining integration includes outline conversion, page paint and clipping,
fill/stroke composition, hinted glyph selection and targeted PDF pixel assets.
