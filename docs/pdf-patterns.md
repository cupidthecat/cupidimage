# PDF patterns

Tiling patterns support colored and uncolored cells, local resources, affine
placement, gaps, overlaps, clipping and transparency. Shading patterns reuse the
[shading renderer](pdf-shading.md). Neither path implies complete PDF support.

## Tiling cells and backdrops

Normal-blended cells can be rendered once and repeated. Colored cells and shading
patterns keep double-precision color, shape and alpha, with a byte image mirror.
Their definitions and initial state are retained so a cache can be rebuilt when
painting moves between RGB and grayscale groups. Source CMYK values are converted
directly into the destination space during that rebuild. Uncolored cells use the
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

## Shading-pattern transparency

A shading pattern's ExtGState can set nonstroking opacity, blend mode, soft mask
and alpha-as-shape for the shading itself. Stroke opacity, line styling and font
parameters do not affect the `sh` operation and are ignored in that dictionary.
The caller's opacity and mask still apply separately when the pattern paints.

Non-Normal shading patterns use a per-object non-isolated knockout group against
the real backdrop. Normal blending can use the isolated cache. Background and
shading samples are separate members of the implicit group, including for mesh
shadings. This distinction matters when alpha or a mask represents shape: part
of the background can remain beneath a partially shaped sample. Ordinary opacity
must not be applied twice where the shading covers its background.

## Bounds and remaining work

Pattern definitions, cached cells and per-object results share the 64 MiB pattern
cache budget. Colored caches and per-object surfaces use 44 bytes per pixel plus
their descriptors; uncolored caches use 20 bytes per pixel. A per-object surface
also consumes the shared transparency budget while it is being evaluated.
The per-object surface is released when that paint operation ends.
Definitions consume retained-stream storage, and each tile invocation consumes
content and invocation work. The full output viewport bounds tile enumeration.
Consequently, very dense backdrop-dependent patterns can exhaust the work budget
even when their ordinary cached equivalent fits.

Changing a cache's destination space consumes the same content, invocation and
shading-evaluation budgets as its first rendering. CMYK, calibrated and ICC group
blending spaces remain unfinished, along with device color-management parameters.

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

`make test-pdf-pattern-colors` adds 30 analytic checks for cache precision,
RGB/gray transitions, selecting and reselecting resources, compressed pattern
definitions and alpha-as-shape in a shading pattern's own soft mask. The shading
cache key includes the initial alpha-as-shape setting. Fault injection covers
retained dictionary copies and cache rebuilds without modifying the caller's
output on failure.

`make test-pdf-shading-patterns` adds 96 analytic checks across all seven shading
types and all 16 blend modes. It covers inner/outer opacity and masks, shape,
backgrounds, deferred validation, irrelevant graphics-state entries and compressed
indirect parameter values. Focused probes of two independent renderers left the
shading pattern's own opacity and blend settings unapplied. These tests therefore
remain analytic evidence, not a claim of reference pixel parity.
