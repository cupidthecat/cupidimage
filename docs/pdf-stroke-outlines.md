# PDF text stroke coverage

`--pdf-text-aa` enables grayscale fills and strokes for supported TrueType, CFF
and Type 1 outlines. The API equivalent is `text_antialias = 1` in the PDF options.
The default remains binary coverage. [Clipping edges](pdf-clipping.md) follow the
same option. Bitmap fallback painting retains its existing geometry policy.
`--pdf-graphics-aa` separately enables [ordinary vector and Type 3 path
coverage](pdf-graphics-coverage.md).

## Stroke geometry

The text adapter transforms the selected or hinted glyph outline into device
coordinates. Quadratic and cubic curves are subdivided using the existing PDF
flatness setting, with a 0.125-pixel ceiling. Stroke widths, dash lengths and
phases are measured in the current graphics user space, not in font space.
A zero-width stroke uses a one-device-pixel pen.

The shared stroke engine builds segment rectangles, joins and caps. Miter joins
fall back to bevel joins at the miter limit. Round caps and joins remain analytic
ellipses under an affine transform. Projecting square caps extend the glyph's
paint bounds even when the miter limit is smaller than their diagonal extent.

Explicit path closing retains short final edges because they still determine
join direction. A move followed by closepath retains a closed point.

A zero-length dash retains the underlying segment's direction, so a square cap
has a defined orientation. A genuinely degenerate subpath paints only with round
caps. A trailing move alone paints nothing. These distinctions follow ISO 32000-1
section 8.5.3.2; line styles and dash parameters are described in section 8.4.3.

## Coverage and painting

The rasterizer measures the union of the stroke primitives inside each pixel.
It does not add overlapping primitive areas or fill a pair of offset borders.
Both shortcuts can produce incorrect coverage, including holes in strokes wider
than the glyph itself.

An eight-pixel tile rejects distant primitives and recognizes completely covered
regions. Boundary pixels are split at polygon vertices, ellipse extrema and
boundary intersections. Between consecutive split positions, the horizontal union
has fixed boundary ordering. Line and ellipse antiderivatives give its area.
No supersampling grid or polygonal approximation of round caps is used.

The geometry still has finite precision. Curve flattening obeys the tolerance
above, while line/ellipse integration uses C99 `long double` arithmetic. Positive
area is quantized with `min(255, floor(area * 256))`. Geometry close to a quantization
boundary can differ by one level when equivalent calculations round differently.

Fill-and-stroke text uses a temporary knockout group when fractional stroke
coverage requires it. Fill and stroke opacity, alpha-is-shape and a caller soft
mask retain their separate roles. The soft mask is applied to the combined result
once. Clipping remains a separate geometric intersection, applied before painting.

## Bounds and ownership

The union rasterizer accepts at most 65,536 convex polygon or ellipse primitives,
coordinates and ellipse axes within +/-65,536 pixels, a crop within +/-32,768
pixels, and at most 16 MiB of output. Each boundary pixel can have at most 256
candidate primitives and 65,536 split positions. The page path also retains its
existing segment and dash limits. Inverse ellipse-axis coefficients are limited
to 1e64 so quadratic intersection products remain finite even where `long double`
has the same range as `double`.

Validation, tile scans, intersections, sorting and integration consume the
caller's work budget. Allocation failure, invalid geometry and exhausted limits
return an error with a null output, not a partially painted stroke. All storage
belongs to the call. The implementation is native C99 with no external rendering
or font-library dependency.

## Tests and remaining work

`make test-pdf-stroke-union` checks analytic rectangles, triangles, circular and
affine elliptical areas, overlaps, thick strokes, input limits, work cancellation
and allocation cleanup. Deterministic mixed-shape cases also compare against an
independent sampling test with an explicit error allowance. Sampling is used only
in that test, not in the renderer.

Page tests cover all three font-outline formats, narrow and wide strokes,
fill/stroke opacity, alpha-is-shape, soft masks, Forms, patterns, deferred text
clips, dash phase cycles, zero-length dashes and Sixel output. Independent convex
polygon clipping checks straight open glyphs with butt and square caps under
rotation, reflection, shear, nonuniform scale and hairline widths.

The owned `assets/pdf/text-strokes.pdf` sample is decoded-unverified. It is a
visual regression aid, not an exact full-page oracle. Broader clipping qualification,
broader vector and curve/hinting qualification, automatic stroke
adjustment and a default smoothing policy remain unfinished. Passing these focused
tests does not establish complete PDF rendering or stroke fidelity.
