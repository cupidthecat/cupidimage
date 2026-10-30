# PDF vector coverage

`--pdf-graphics-aa` enables grayscale path fills and strokes. The API field is
`graphics_antialias = 1`. It applies to page graphics, Forms, patterns, soft masks
and paths drawn by Type 3 glyph programs. The default remains binary coverage.
Use `--pdf-text-aa` separately for TrueType, CFF and Type 1 outlines. Either option
enables fractional clipping paths and Form boundaries.

## Geometry and paint

Filled paths use nonzero winding or even-odd interior rules, including implicit
closure of open subpaths. A filled point subpath encloses its device pixel, as
required by ISO 32000-2 section 8.5.3.3.1; a trailing move alone remains empty.
The point rule applies to binary fills and clipping too. Stroke closure remains explicit. Cubic curves retain
the existing adaptive flattening policy: the current flatness setting, capped at
0.25 device pixels. The `c`, `v` and `y` operators share that implementation.

The fill rasterizer integrates line-bounded regions within each pixel. Styled
strokes use the [analytic stroke union](pdf-stroke-outlines.md), including affine
round caps, joins, dash patterns and one-device-pixel hairlines. Neither path
uses a fixed supersampling grid. Coverage follows the existing quantization rule,
`min(255, floor(area * 256))`, subject to finite floating-point precision.

[Clipping](pdf-clipping.md) intersects geometry before rasterization. Independently
multiplying draw and clip alpha values would give the wrong answer for coincident
or disjoint subpixel shapes. The draw's fill rule is retained during intersection.

Combined fill-and-stroke operators paint one object. Fractional stroke coverage
uses a temporary knockout group so fill opacity, stroke opacity, alpha-is-shape
and soft masks keep their PDF meanings. A soft mask applies once to the combined
result. Colors, patterns and blend modes use the existing paint pipeline.

## Limits

Paths use bounded storage for up to 65,536 segments and retain the shared page
work budget. See [dense paths](pdf-dense-paths.md) for storage and coincident-edge
handling. Fill and clip
coverage use the region rasterizer's 65,536-edge limit, 1,024 per-pixel crossings,
16 MiB output bound and coordinate/crop bounds of 32,768 device pixels. Strokes
retain the separate limits in the stroke guide. Combined painting may need a
full-page transparency surface and can fail its memory limit even for a small
path. Invalid geometry, allocation failure or exhausted work returns a rendering
error rather than a successful partial page.

## Verification and scope

`make test-pdf-graphics-antialias` checks fractional rectangles, clipped even-odd
holes, narrow and wide strokes, closed points, fill/stroke composition, Forms,
patterns, Type 3 paths and Sixel output. Curve cases compare shorthand operators,
reverse traversal and the area of a cubic with a known polynomial integral.
Affine fills also use an independent rational polygon-clipping oracle. Four
profiles match byte-for-byte; a rotated case permits one coverage level at a
quantization boundary. Allocation-failure checks cover the new painting path
and its cleanup.

`assets/pdf/graphics-coverage.pdf` is an owned, decoded-unverified visual sample.
Rebuild it with `scripts/generate_pdf_graphics_coverage_fixture.py`.

The option tests also exercise every combination of the five PDF rendering
flags through page and asset probes. Audit and simulated-review reports preserve
the chosen profile; leaving the graphics option off preserves older profile keys.
These are automated fixture checks, not manual ratings of the PDF corpus.

Image interpolation, shading-edge coverage, automatic stroke adjustment and
complete font/color/transparency fidelity remain separate work. These checks do
not establish full PDF rendering parity.

The geometry and graphics-state rules follow ISO 32000-1 sections 8.4 and 8.5;
Type 3 glyph programs are described in section 9.6.5. Combined painting follows
section 11.7.4.4. Device antialiasing and quantization are rendering policies,
not a pixel profile prescribed by the standard.
