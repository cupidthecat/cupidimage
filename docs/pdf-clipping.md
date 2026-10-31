# PDF clipping coverage

`--pdf-text-aa` enables fractional coverage for text outlines, clipping paths and
Form boundaries. The PDF options API uses `text_antialias = 1`. Omitted options
and zero keep the existing binary rendering path. `--pdf-graphics-aa` also enables
fractional clipping, alongside [vector coverage](pdf-graphics-coverage.md).

Image sampling and bitmap fallback painting still use their existing geometry
policies. A smoother clip edge does not make those renderers fully antialiased.

## Geometry before coverage

Clipping intersects regions, not their independently rasterized alpha values.
Applying the same quarter-pixel clip twice must retain a quarter pixel. Two
separate quarter-pixel regions inside one pixel must have an empty intersection.
Multiplying alpha masks gives the wrong answer in both cases.

The renderer retains closed, directed contours in device coordinates. Each
region uses its own nonzero or even-odd fill rule. Text clipping accumulates
selected glyph outlines through the text object and applies their nonzero
winding region at `ET`, after painting. The result is intersected with the clip
active at that point. `q` and `Q` retain and restore immutable geometry.

A glyph with no outline does not change the clip. A nonempty outline that
collapses under its transform can still create an empty clip. Encoded glyph
selection decides which case applies; character code 32 is not a special rule.
Type 3 text rendering modes retain their separate PDF behavior.

Form bounding boxes become geometric clip regions in their transformed
coordinates. Patterns and soft-mask forms inherit the coverage option through
their own rendering contexts. Group compositing does not apply an inherited
fractional clip a second time after its contents have already been clipped.

These rules follow ISO 32000-1 and ISO 32000-2 sections 8.5.4 and 9.3.6. The
transparency rules remain separate from clipping geometry.

## Rasterization

Boundary pixels are split at vertices, line intersections, ellipse intersections
and crossings of pixel edges. Vertices outside a pixel do not create local
split positions; horizontal boundaries that span it still do. A horizontal sweep tracks each region's winding
number. It integrates only intervals inside every required region. Straight
boundaries use linear area integrals; round stroke caps and joins use affine
ellipse integrals. The renderer does not use a supersampling grid for this step.

Curves still require finite-precision subdivision. Text contours use the existing
flatness setting with a 0.125-pixel ceiling. Ordinary path contours retain their
0.25-pixel ceiling. Smaller flatness settings are honored. The intersection
calculations use C99 `long double`; output coverage is
`min(255, floor(area * 256))`.

Antialiased glyph fills and strokes are intersected with the clip geometry before
painting. Pixels whose clip is quantized to full coverage retain the existing
glyph raster bytes. Partial intersections cannot increase the original glyph
coverage. The existing fill scan converter and the geometric boundary integrator
have different finite-precision policies, so boundary/profile equivalence still
needs broader qualification.

Bitmap fallback clips use the transformed bitmap-row outlines. That does not
replace missing-font matching or turn the fallback into an accurate font program.

## Ownership and bounds

Clip nodes retain their parent regions. Repeated identical constraints reuse the
same node and page mask rather than deepening the chain or multiplying coverage.
Zero-length edges are omitted from retained geometry.

- At most 63 distinct region nodes per retained chain and 65,536 retained edges.
- A draw intersection allows 64 regions, 65,536 line edges and 65,536 congruent
  ellipse contours. Boundary-pixel crossings and ellipse candidates are limited
  to 1,024; split positions are limited to 65,536.
- Region coordinates and crop boundaries stay within +/-32,768 pixels. Ellipse
  axis coefficients stay within +/-65,536, with bounded inverse coefficients.
- A page clip mask is limited to 16 MiB. Retained masks, pending text contours,
  winding storage and geometry share the existing 64 MiB clipping budget.
- Parsing, collection, allocation-related copies, intersections, sorting and
  rasterization consume the page work budget.

Allocation failure, exhausted work or unsupported ranges fail the page. They do
not silently switch to a binary clip or return a partially clipped page. The
standalone rasterizers clear their output pointers on failure.

## Rectangular clip cost

A single closed, axis-aligned rectangle has a direct area calculation. Its
intersection with a chain of rectangles is another rectangle, so the renderer
uses the retained bounds instead of a general polygon sweep. Full-coverage tests
for glyph crops also use those bounds. Empty intersections remain empty.

The optimization requires exact edge connectivity and alternating horizontal
and vertical edges. A trapezoid, bow-tie, multiple contours or nonrectangular
parent keeps the general path. It does not replace arbitrary shapes with their
bounding boxes or increase the work limit. An owned page with 40 saved-state
rectangular clips now completes under the existing budget and matches the
single-clip result.

Canvas-sized rectangular clips retain the existing mask unchanged. Their four
edges consume work, but no new mask is constructed. This avoids counting
redundant page rectangles against the 4,096 mask-construction limit. Other clips
still consume that limit, and resource exhaustion remains an error. See
[dense paths](pdf-dense-paths.md) for the scanline and bounded-raster changes.

## Verification

`make test-pdf-clip-regions` runs:

- 6,912 analytic, independent-geometry, boundary and allocation/cancellation checks;
- 299 clip-node scope, depth, reuse and ownership checks;
- 82 page checks and 613 document allocation-failure checks.

The page cases cover repeated and disjoint subpixel clips, nonzero/even-odd holes,
all four text-clipping modes, empty and collapsed glyphs, saved graphics states,
clipped glyph fills and round strokes, and isolated/nonisolated Form boundaries. Dense 500- and 1,500-glyph clips
are compared byte-for-byte with separately clipped rows under the unchanged
page work budget.
Separate font, stroke, pattern, soft-mask and transparency regressions remain in
the full suite.

`assets/pdf/fractional-clipping.pdf` is an owned visual sample. Rebuild it with
`scripts/generate_pdf_fractional_clip_fixture.py`. Its complete page is
decoded-unverified; the focused tests provide the area and ownership evidence.

This is not complete PDF raster fidelity. Image resampling, wider curve/profile comparisons, automatic stroke adjustment and
other font/color/transparency requirements remain open. No full manual corpus
rating pass is part of these checks.
