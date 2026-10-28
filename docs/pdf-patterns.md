# PDF patterns

Tiling patterns support colored and uncolored cells, local resources, affine
placement, gaps, overlaps, clipping and transparency. Shading patterns reuse the
[shading renderer](pdf-shading.md). Neither path implies complete PDF support.

## Tiling cells and backdrops

Normal-blended cells can be rendered once and repeated. Their cache stores RGB
bytes with separate double-precision shape and alpha. Uncolored cells use the
caller's native device color when the cache is painted. Color-setting operators
and ordinary color images are not permitted in an uncolored definition.

A non-Normal blend mode can depend on what lies beneath each tile. Such a pattern
retains its definition and initial graphics state instead of treating the first
cached cell as the finished result. When an object is painted, the renderer
executes the visible tiles into one non-isolated, non-knockout group against that
object's backdrop. It removes the initial backdrop contribution before applying
the caller's opacity, blend mode, soft mask and clipping.

The per-object result keeps double-precision color, shape and alpha. It is not
reused for a later paint operation or a later glyph. Overlapping tiles share one
group, so the caller's opacity is applied once rather than once per tile. Pattern
phase remains anchored to the defining content stream's initial coordinates.
Nested forms and text use their existing resource and graphics-state rules.

## Bounds and remaining work

Pattern definitions, cached cells and per-object results share the 64 MiB pattern
cache budget. A per-object surface uses 44 bytes per pixel plus its descriptor;
it also consumes the shared transparency budget while it is being evaluated.
The per-object surface is released when that paint operation ends.
Definitions consume retained-stream storage, and each tile invocation consumes
content and invocation work. The full output viewport bounds tile enumeration.
Consequently, very dense backdrop-dependent patterns can exhaust the work budget
even when their ordinary cached equivalent fits.

The ordinary cell cache still stores RGB bytes. Native color handling inside
those caches, including conversion into a different blending space, remains
incomplete. Shading-pattern ExtGState transparency restrictions also remain.
CMYK, calibrated and ICC group blending spaces are separate unfinished work.

## Tests and specification

`make test-pdf-pattern-backdrops` runs 43 analytic checks covering changing backdrops, caller opacity,
repeated painting, separable and nonseparable modes, transformed cells, nested
forms, text, strokes, soft masks, CMYK paint into a gray group, overlapping tiles
and deferred validation. The allocation-failure suite includes both colored and
uncolored backdrop-dependent patterns.

The analytic expectations follow
[ISO 32000-1, section 11.6.7](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf).
In the tested Multiply cases, two independent renderers instead produce a uniform
gray cached pattern over red and green backdrops. Those outputs disagree with the
non-isolated evaluation described by the specification. The new analytic cases
are not reported as reference passes, and reference tolerances are not widened.
The optional `--reference-only` diagnostic compares the analytic pixels with an
independent renderer and fails on this known discrepancy.
