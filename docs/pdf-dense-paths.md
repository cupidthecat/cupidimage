# Dense PDF paths and clipping work

Paths now grow beyond the original 4,096-segment inline buffer, up to a hard
limit of 65,536 segments. Growth is checked before allocation. Fills, strokes,
clipping and outline text use the same storage interface. A compound path stays
one painting operation, preserving winding, even-odd holes and opacity.

Flattening transfers ownership of the original curve storage before building
the flattened path. Forms, patterns, Type 3 glyph programs and soft masks detach
the parent's path when copying render state. Clearing a path or leaving a render
scope frees its extended storage, including on errors.

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

## Localized clipping

Clip coverage still uses the full-sized zeroed coverage buffer, subject to the
existing 16 MiB per-mask and 64 MiB shared clipping-storage limits. Geometric
tests and bitmap population visit only the intersection of the clip's bounds
with the canvas. Bounds restrict the work; they do not replace the actual
clipping geometry.

Work accounting charges the bounded pixel area and bitmap clearing, followed
by the existing analytic rasterizer charges for nonrectangular clips. The shared
page budget is unchanged. Empty and off-canvas clips still clear their bitmaps,
and allocation or work exhaustion still fails the page rather than returning a
partial rendering.

## Tests and remaining limits

Run `make test-pdf-dense-paths` and `make test-pdf-clip-regions`. The tests cover
storage growth, the segment limit, curve ownership, nested rendering scopes,
combined painting, winding/parity, separate intersection operands, allocation
failure and exact clipping-work admission boundaries. Repeating a small clip
400 times on a letter-sized page no longer spends a full page of geometric tests
on each invocation. Both rectangular and triangular clips are checked.

Regenerate the owned assets with
`scripts/generate_pdf_dense_path_fixtures.py`. The dense-vector-path.pdf asset
has 1,300 overlapping rectangles painted once. Its literal red raster also
matches an independent 96-DPI render exactly. The localized-clip-work.pdf asset
has an exact native oracle using integer device boundaries. An independent
antialiased render adds coverage in one adjacent column: 48 color channels
differ by 32. That boundary-policy difference is recorded, not hidden by a
comparison tolerance.

A focused audit of the pinned bug1703683_page2_reduced.pdf corpus page now
renders with all five PDF options enabled. It still differs from the independent
raster: mean absolute channel error 0.78596, maximum 169, and 3.183% of color
channels different. bug1721218_reduced.pdf now passes the earlier clipping limit
but still fails a shading-work limit. Neither result establishes full rendering
parity, and the broader manual review remains deferred.

Path construction and painting follow ISO 32000-1 sections 8.5.2 and 8.5.3;
clipping follows section 8.5.4. Device antialiasing and coverage quantization
remain renderer policies. See [vector coverage](pdf-graphics-coverage.md) and
[clipping](pdf-coverage.md) for the related limits and options.
