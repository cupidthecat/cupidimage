# Dense PDF paths and clipping work

Paths now grow beyond the original 4,096-segment inline buffer, up to a hard
limit of 65,536 segments. Growth is checked before allocation. Fills, strokes,
clipping and outline text use the same storage interface. A compound path stays
one painting operation, preserving winding, even-odd holes and opacity.

Flattening transfers ownership of the original curve storage before building
the flattened path. Forms, patterns, Type 3 glyph programs and soft masks detach
the parent's path when copying render state. Clearing a path or leaving a render
scope frees its extended storage, including on errors.

## Binary scanlines

Binary fills and clips sort the active intersections once per device row, then
walk the pixels in order. They retain the previous float intersection arithmetic,
endpoint rule and winding/parity decisions. The renderer no longer tests every
edge separately at every pixel. A bounded heapsort charges its comparisons and
moves to the shared work budget. Allocation or work failure rejects the page,
including ordinary fills.

The scanline tests compare 265,807 winding and budget cases against a separate
per-pixel ray-crossing implementation. The former rejection case with 500
coincident page rectangles now renders correctly under the same budget.

## Coincident edges

For large inputs, the analytic region rasterizer folds exactly identical directed
edges within the same region into a weighted edge. Its weight retains the full
winding multiplicity, including even-odd parity. Coordinates are not rounded,
nearly coincident edges are not merged, and different clipping operands remain
separate. Hash-table construction and collision probes consume the existing work
budget. Oppositely directed edges retain their own entries.

The rasterizer still limits input to 65,536 edges and per-pixel crossings to
1,024 after folding. A path can therefore fit its segment limit and still exceed
a later geometry or work limit. Ellipses are not folded by this optimization.

When no boundary crosses a device pixel, the analytic rasterizer evaluates its
constant winding directly. Boundary pixels still use the area integrator, with
unchanged coordinates and quantization.

## Localized clipping

Clip coverage still uses the full-sized zeroed coverage buffer, subject to the
existing 16 MiB per-mask and 64 MiB shared clipping-storage limits. Geometric
tests and bitmap population visit only the intersection of the clip's bounds
with the canvas. Bounds restrict the work; they do not replace the actual
clipping geometry.

Work accounting charges the bounded pixel area, followed by the existing
analytic rasterizer charges for nonrectangular clips. Fresh path and Form masks
are already zeroed by allocation, so they do not clear the bitmap a second time.
Text masks may contain provisional glyph winding bits; rebuilding those masks
still clears the bitmap and charges for that work. The shared
page budget is unchanged. Empty and off-canvas clips retain zeroed bitmaps,
and allocation or work exhaustion still fails the page rather than returning a
partial rendering.

A connected axis-aligned rectangle covering the entire device canvas is a no-op
for clipping. It preserves the inherited clip and Form boundaries without
allocating a new mask or consuming a mask-construction call. Its four edges still
consume work units. This requires the actual rectangle, not merely a matching
bounding box; diamonds, sheared paths and multiple contours take the normal path.
The mask-construction limit remains 4,096. Tests repeat a page rectangle 7,072
times with both full-page and fractional inherited clips.

Antialiased path painting also restricts its raster window to the inherited
clip's bounds before allocating coverage. The original path edges and geometric
intersection remain unchanged inside that window.

For a local draw intersection, retained clip nodes now carry bounds for each
closed contour. Contours entirely outside the crop are omitted as whole loops;
individual edges are never dropped based only on their position. Contours that
enclose or touch the crop remain in the intersection, including holes and
opposite windings. Index storage counts against the existing 64 MiB budget, and
both lookup passes consume work. The full geometry remains available for other
crops and saved states.

The crop tests compare 512 complete rasters with their unpruned equivalents,
including nonrectangular contours, holes, parent regions and surrounding loops.
Another 4,000 checks exercise work exhaustion and allocation cleanup. A page
with 3,072 separate clipped shapes is compared against independently clipped
shapes.

## Shading scans

Function-based, axial and radial shading restrict their scan to the current
clip's canvas bounds. Analytic clips provide geometric bounds directly. Binary path clips retain conservative pixel bounds from rasterization and
intersect those bounds with their parent. Other binary clips still use a
budgeted byte scan of the bitmap; a byte that straddles rows keeps a conservative
horizontal range. Every candidate pixel still passes the
compositor's actual clip and Form tests. Soft masks do not restrict these bounds,
because an object with zero opacity can still contribute shape to knockout.

Each scanned pixel consumes one work unit, including pixels outside a shading's
domain or BBox. Function and tint evaluation retain their additional costs. The
shared shading budget stays at 134,217,728 units. The owned clipped workload of
192 shading invocations now fits. A paired small-canvas test sets a 10,000-unit
budget: 192 clipped invocations leave exactly 2,128 units, while the unclipped
case exhausts it. The expensive full-page exhaustion check remains available
through `scripts/test_pdf_shading_work.py` with `--slow-limit`; it is excluded
from the default CI workload.

## Tests and remaining limits

Run `make test-pdf-dense-paths`, `make test-pdf-clip-regions` and
`make test-pdf-shading-work`. The tests cover
storage growth, the segment limit, curve ownership, nested rendering scopes,
combined painting, winding/parity, separate intersection operands, allocation
failure and exact clipping-work admission boundaries. Repeating a small clip
400 times on a letter-sized page no longer spends a full page of geometric tests
on each invocation. Both rectangular and triangular clips are checked.

The repeated-page-clips.pdf asset applies 7,072 page rectangles over a smaller
inherited clip. Its literal red-and-white raster is checked without tolerance.

Regenerate the owned assets with
`scripts/generate_pdf_dense_path_fixtures.py`. The dense-vector-path.pdf asset
has 1,300 overlapping rectangles painted once. Its literal red raster also
matches an independent 96-DPI render exactly. The localized-clip-work.pdf asset
has an exact native oracle using integer device boundaries. An independent
antialiased render adds coverage in one adjacent column: 48 color channels
differ by 32. That boundary-policy difference is recorded, not hidden by a
comparison tolerance.

The bounded-shading-work.pdf asset paints an integer-aligned green rectangle
192 times on a letter-sized page. It has a literal native pixel oracle. The
independent antialiased renderer accumulates coverage in an adjacent column;
16 color channels differ by 252 after the repeated painting. Shading
tests include all three function-based types, odd-width bitmaps, sparse and empty
clips, and allocation failures. A separate before/after check found identical
bytes on 72 small pages, including fractional and triangular clips.

A focused audit of the pinned bug1703683_page2_reduced.pdf corpus page now
renders with all five PDF options enabled. It still differs from the independent
raster: mean absolute channel error 0.78596, maximum 169, and 3.183% of color
channels different. bug1721218_reduced.pdf now renders in both the default profile and with all five
options enabled. It passes the repeated page-clip limit, a 36,250-edge binary
clip, thousands of local contour intersections and multiple shading-pattern
caches without increasing those resource limits. The all-options raster still
differs from the independent 96-DPI image: mean absolute channel error 1.12680,
maximum 156, and 3.7192% of color channels different.
Neither result establishes full rendering
parity, and the broader manual review remains deferred.

Path construction and painting follow ISO 32000-1 sections 8.5.2 and 8.5.3;
clipping follows section 8.5.4. Device antialiasing and coverage quantization
remain renderer policies. See [vector coverage](pdf-graphics-coverage.md) and
[clipping](pdf-coverage.md) for the related limits and options.
