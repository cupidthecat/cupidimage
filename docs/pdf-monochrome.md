# PDF monochrome TrueType rendering

With `--pdf-tt-hinting` enabled and `--pdf-text-aa` disabled, TrueType fills use
fixed-point monochrome scan conversion. Narrow strokes can receive dropout pixels
when the font's instructions enable them. Unhinted rendering, grayscale coverage,
CFF and Type 1 retain their existing paths. Stroke and text-clip geometry still
come from the hinted outline; dropout pixels do not expand a clipping path.

Try the owned sample:

```sh
./bin/cupidimage --pdf-tt-hinting --sixel assets/pdf/native-monochrome-hinting.pdf
```

## Instruction state

`SCANCTRL` evaluates its threshold and conditional enable/disable bits at the
current size. Threshold zero disables dropout control; 255 enables it. Other
thresholds preserve the previous state when no selected condition matches.
Preparation state is restored at glyph entry. A disabled control selects no-dropout conversion regardless of the stored `SCANTYPE` value.

`SCANTYPE` selects simple dropout control (0/1), no dropout control (2/3/6/7), or
smart dropout control (4/5). Odd simple/smart modes exclude stubs. Each executed
glyph program records its effective policy at the first point. Composite children
retain their contour markers; a parent program replaces the first marker without
erasing later child markers. This is the selected legacy raster policy, not a
claim that every font engine resolves composite policies identically.

Monochrome page hinting supplies rotation and stretch flags derived from the
normalized device matrix. The device's downward Y axis does not itself count as
rotation. Off-axis terms or a 180-degree turn set rotation; reflection alone does
not. Unequal axis lengths or nonorthogonal axes set stretch. These flags are part
of the prepared-state cache key. The grayscale instruction profile is unchanged.

The conditional disable bits follow the official
[OpenType instruction definitions](https://learn.microsoft.com/en-us/typography/opentype/spec/tt_instructions)
and [TrueType instruction manual](https://developer.apple.com/fonts/TrueType-Reference-Manual/RM05/Chap5.html):
bits 12 and 13 disable control *unless* the glyph is rotated or stretched. Some
legacy engines reverse these two conditions. Those differences are tested against
the published rules, not counted as independent engine agreement.

## Raster and ownership

The private converter uses the pinned, attributed implementation described in
[its adaptation notes](../third_party/freetype-monochrome/README.md). It consumes
F26Dot6 points and supports line, quadratic and cubic segments. TrueType supplies
quadratics. High-precision conversion is selected below 24 ppem. Per-contour scan
markers override the initial mode as contours are processed.

Dropout selection can paint just outside the geometric outline bounds. The page
fill crop includes one extra pixel on each side before clipping to the image.
Coverage remains binary, then uses the ordinary PDF color, opacity, clip and
compositing paths. PDF text widths and advances are not changed.

Each invocation owns its worker, pool and bitmap. The wrapper accepts at most
65,535 points, dimensions up to 32,767 pixels and 16 Mi pixels of output. Coordinates
are bounded to ±2,097,152 in F26Dot6 units. Contour ends and scan tags are validated.
Output initialization, outline conversion and raster loops consume the shared
work budget. Failure discards partial output and frees all owned storage.

## Tests and limits

`make test-pdf-monochrome` runs:

- 2,621,960 scan-control state and condition checks;
- analytic raster boundaries and diagonals, empty outlines, work exhaustion,
  undersized pools and 4,096 bounded curve-structure cases;
- 384 allocation/ownership checks for the raster wrapper;
- 3,776 stored independent bitmap cases, including curves, fractional phases,
  all scan modes and per-contour overrides;
- authored PDF checks for narrow stems, stubs, composite overrides, transformed
  conditions, unchanged non-monochrome profiles, strokes, clipping and opacity;
- page-level allocation failure tests and fixture/source provenance checks.

The optional `scripts/test_pdf_monochrome_reference.py` compares against a separate
font-engine library. Its `--library` option selects a specific shared library;
`--generate` refreshes the stored vectors after review. Engines older than 2.14.3
are skipped because their clipped-line intersection rule differs. Stored vectors
run without optional Python packages or a system font library.

This completes the selected legacy monochrome path, not all TrueType rendering.
Modern subpixel policies, variation behavior and broader real-font comparisons
remain separate work. Raster agreement alone does not establish complete PDF
text fidelity.
