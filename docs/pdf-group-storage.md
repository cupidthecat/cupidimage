# PDF transparency group storage

Nested text and Type 3 glyphs can create several implicit transparency groups
while drawing only a small part of a page. Reserving full-page native color,
alpha and shape planes for each group made some ordinary pages exceed the
transparency memory limit before any pixels were painted.

Native samples now use blocks of 16,384 pixels. A sample retains separate alpha
and shape values plus three or four double-precision color components. Untouched
samples are zero; no color values are quantized or reconstructed from the RGBA
preview. The group equations, blending spaces and knockout order are unchanged.

## Backdrops and ownership

Text groups, Type 3 glyph groups, combined fill/stroke objects and deferred
pattern surfaces resolve their initial backdrop when a pixel first contributes
shape. A zero-opacity object can still have shape, so it is not skipped. Eager
backdrop groups still populate their samples during initialization. Isolated
surfaces start transparent unless they have an explicit background.

The RGBA view remains page-sized. The shared 256 MiB transparency budget includes
that view, the block pointer index and every allocated sample block. A block is
charged before it becomes visible to the renderer. Allocation or budget failure
propagates through painting and releases partial surfaces; it does not silently
omit the failed pixels. Inline images also propagate deferred painting failures
when EI is the last operation in the stream.

Cached pattern tiles keep their existing borrowed dense planes. Completed deferred
surfaces transfer their actual storage charge to the separate 64 MiB pattern
cache, become read-only and release that charge with the cache entry. Color,
alpha and shape accessors handle both representations. No pattern matrix,
interpolation rule, font outline or paint operator changes as part of this storage
work.

These limits have not increased. An untouched 5,000-square group can now fit,
while an RGBA surface alone exceeding the cap is still rejected. A six-level
CMYK mask can also fit when its ancestors have not yet painted. Painting dense
ancestor surfaces before descending still reaches the same cap.

## Verification

`make test-pdf-sparse-groups` runs storage ownership/boundary checks and exact
224 page checks and 891 allocation checks, including comparisons between nested
Type 3 glyphs and flattened leaf programs. The
page cases cover RGB, gray and CMYK groups, zero/partial/full opacity, knockout,
antialiasing and bounded recursion. The owned `nested-type3-groups.pdf` also has
a complete-page control. Existing mask, color-space, pattern, text and combined
paint suites remain separate.

The storage unit test checks 786,554 cases, including empty reads, dense values,
partial final blocks, shared-budget exhaustion, frozen cache ownership and every
allocation failure. The mask-work suite checks both the newly admitted empty
ancestor stack and a densely painted stack that remains over budget.

The original acyclic nested-Type-3 corpus page now renders at 800 by 1,120 pixels.
Its previous failure occurred with 236,544,000 bytes live, before another
39,424,000-byte group could be admitted. A half-size version remains byte-identical
to the preceding renderer in all 32 combinations of the five PDF rendering
options.

The recovered full-size page is not a fidelity match. Its measured mean absolute
channel error is 1.17149, with maximum error 255, against the tested comparison
profile. A second interpreter also differs. Those differences are not counted as
agreement, and the original document is not redistributed.

The governing group and implicit-object rules are ISO 32000-1 sections 11.4.8
and 11.7.4. Pattern context follows section 8.7.2. See the
[transparency guide](pdf-transparency.md), [group work limits](pdf-group-work.md)
and [standards index](pdf-standards.md) for the broader implementation scope.
