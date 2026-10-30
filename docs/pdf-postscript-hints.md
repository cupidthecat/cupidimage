# PostScript font hint data

CFF version-1 fonts retain their Private DICT hint parameters. An optional Type 2
interpreter entry point also captures stem declarations and hint masks. The page
renderer uses unhinted CFF and Type 1 outlines by default. The
[grid-fitting core](pdf-postscript-gridfit.md) consumes these inputs through an
explicit [CFF page option](pdf-cff-page-hinting.md).

## CFF private dictionaries

Each font dictionary has its own BlueValues, OtherBlues, FamilyBlues,
FamilyOtherBlues, StemSnapH and StemSnapV arrays. CFF delta arrays are decoded to
absolute values. The parser also retains BlueScale, BlueShift, BlueFuzz, StdHW,
StdVW, ForceBold, LanguageGroup and ExpansionFactor, with the specified defaults.
The existing initialRandomSeed handling is unchanged.

The arrays have fixed limits: 14 values for BlueValues and FamilyBlues, 10 for
OtherBlues and FamilyOtherBlues, and 12 for each stem-snap array. Blue arrays must
contain pairs. OtherBlues must follow BlueValues; FamilyOtherBlues must follow
FamilyBlues. ForceBold requires an integer boolean. Counts, numeric bounds and
scalar domains are checked before storing a value.

Malformed hint fields receive their default value and an invalid-field bit.
A later valid definition replaces both the value and that diagnostic. This keeps
an otherwise usable unhinted font readable. Structural DICT failures, such as
truncated numbers, still fail parsing. Cross-field rules about zone overlap,
BlueScale and zone height are not yet qualified for grid fitting.

## Type 1 private dictionaries

Type 1 fonts retain the same fourteen fields from literal private-dictionary
definitions. Unlike CFF, their arrays contain absolute values, StdHW/StdVW are
single-element arrays, and ForceBold uses a PostScript boolean. The reader accepts
literal array/procedure objects and verified local aliases for `def`. Nested
unrelated dictionaries and procedure bodies do not supply font hints.

Computed expressions are marked unresolved and invalid rather than taking the
first literal as their result. A later literal definition clears those flags.
For example, a conditional StemSnap array remains unresolved. The unhinted
outline path stays available. Arbitrary PostScript evaluation, inherited definer
aliases remain unfinished. The standalone [Type 1 fitting core](pdf-postscript-gridfit.md)
uses the resolved fields required by its selected profile.

## Type 2 capture

`cupidimage_pdf_type2_outline_hints` captures up to 96 horizontal and vertical
stems in declaration order. It removes an optional initial width before reading
the stem operands, decodes each operator's delta sequence, and retains the raw
negative widths used for edge hints. Local and global subroutines share the same
hint context as their caller.

Mask events record their outline-segment position, stem count and up to 12 mask
bytes. Counter masks remain separate from active-hint masks, in their original
priority order. Bits retain their most-significant-first ordering. Empty masks,
nonzero unused bits, late stems, reversed horizontal/vertical declaration order
and undefined negative widths receive diagnostics rather than changing the
existing unhinted recovery behavior.

The trace is optional and caller-owned. The ordinary outline API requests no
trace and makes no additional trace allocations. Capture is bounded by the
existing charstring work limit and 65,536 events. Allocation or structural
failure releases both the outline and trace. A composite's components must be
captured separately so their hint contexts are not combined accidentally.

## Type 1 capture

`cupidimage_pdf_type1_outline_hints` captures one charstring's stem declarations,
three-stem groups, hint replacements, dotsection toggles and flex parameters.
Stem coordinates include the sidebearing. Events retain their order and outline
segment position, including declarations after drawing starts. Replacement
contexts are not collapsed into a single CFF mask. Composite parents retain
component references; each component needs its own trace.

Tracing is optional and owns its event storage. Failure releases both the trace
and outline. The budgeted entry point shares a caller's remaining work while
keeping the 65,536-token limit per charstring. The ordinary outline API keeps its
existing composite assembly and makes no trace allocations.

Counter-control OtherSubrs 12/13 retain their chunked group data in separate
trace events. Those events preserve encoded deltas and signed widths, not absolute
stem coordinates. They do not change the current fitting map. See
[Type 1 counter data](pdf-type1-counter-data.md) for bounds and checks.

## Tests and remaining work

`make test-pdf-cff` includes 1,542 primitive checks, 36 authored private-parameter
checks and 28 authored stem/mask capture cases. The capture tests also compare
outlines with tracing enabled and disabled. Allocation-failure tests exercise
mask-buffer growth, outline-buffer growth and truncated-program cleanup.

With FontTools installed, the suite compares 308 private fields from 22 pinned
fonts and independently decodes 3,226 glyphs containing 13,717 stems and 2,759
masks. These checks establish agreement on decoded inputs, not rendered pixels.

The separate grid-fitting core has bounded outline and glyph-bitmap tests.
Composite placement and CFF/Type 1 page integration have focused checks. Type 1
computed parameters, flex-depth handling and broader profile qualification remain
open. See the [Type 1 page guide](pdf-type1-page-hinting.md).
`--pdf-tt-hinting` applies only to TrueType and does not enable these stages.

`make test-pdf-type1` includes 39 authored private-hint cases. Optional comparisons
against installed PFB fonts use FontTools:

    python3 scripts/compare_pdf_type1_hints.py bin/test_pdf_type1 /path/to/pfb/fonts

One local set of 75 fonts matched 999 fields and reported 51 unresolved conditional
expressions. Those expressions are excluded from the agreement count. These are
input-parser checks, not hinted-outline or pixel comparisons.

Type 1 trace tests include 26 authored programs, 119 budget/ownership checks and
allocation-failure coverage. An optional independent operand-stream comparison is:

    python3 scripts/compare_pdf_type1_trace.py bin/test_pdf_type1 /path/to/pfb/fonts

The same 75-font set matched 61,089 glyph traces containing 436,559 events.
Segment positions have separate authored tests. This comparison does not test
hinted outlines, pixels, arbitrary OtherSubrs or multiple-master interpolation.
Unhinted output for all 61,089 glyphs also remained unchanged from the earlier
parser. These installed fonts are not bundled or required for the default suite.

## Specifications

- Adobe [Technical Note 5176, CFF](https://pdfa.org/norm-refs/5176.CFF_.pdf), Private DICT operators and delta operands.
- Adobe [Technical Note 5177, Type 2](https://pdfa.org/norm-refs/5177.type2_.pdf), sections 3.1 and 4.3 and implementation limits.
- Adobe [Type 1 Font Format](https://pdfa.org/norm-refs/Type1Fonts.pdf), private hint parameters and edge hints.
- Adobe [Technical Note 5015](https://pdfa.org/norm-refs/5015.Type1_Supp.pdf), counter control and supplemental parameters.
