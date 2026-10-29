# PostScript font hint data

CFF version-1 fonts retain their Private DICT hint parameters. An optional Type 2
interpreter entry point also captures stem declarations and hint masks. The page
renderer still draws unhinted CFF and Type 1 outlines. A separate [grid-fitting core](pdf-postscript-gridfit.md) now consumes them,
but it is not connected to pages and does not change page pixels.

## Private dictionaries

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

## Tests and remaining work

`make test-pdf-cff` includes 1,501 primitive checks, 36 authored private-parameter
checks and 28 authored stem/mask capture cases. The capture tests also compare
outlines with tracing enabled and disabled. Allocation-failure tests exercise
mask-buffer growth, outline-buffer growth and truncated-program cleanup.

With FontTools installed, the suite compares 308 private fields from 22 pinned
fonts and independently decodes 3,226 glyphs containing 13,717 stems and 2,759
masks. These checks establish agreement on decoded inputs, not rendered pixels.

The separate grid-fitting core has bounded outline and glyph-bitmap tests.
Still needed: Type 1 private-parameter capture, flex-depth handling, component
placement, page integration and broader profile qualification.
`--pdf-tt-hinting` applies only to TrueType and does not enable these stages.

## Specifications

- Adobe [Technical Note 5176, CFF](https://pdfa.org/norm-refs/5176.CFF_.pdf), Private DICT operators and delta operands.
- Adobe [Technical Note 5177, Type 2](https://pdfa.org/norm-refs/5177.type2_.pdf), sections 3.1 and 4.3 and implementation limits.
- Adobe [Type 1 Font Format](https://pdfa.org/norm-refs/Type1Fonts.pdf), private hint parameters and edge hints.
- Adobe [Technical Note 5015](https://pdfa.org/norm-refs/5015.Type1_Supp.pdf), counter control and supplemental parameters.
