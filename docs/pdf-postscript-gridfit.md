# PostScript outline grid fitting

Bounded CFF and Type 1 grid-fitting cores consume the
[retained hint data](pdf-postscript-hints.md) and return owned device-space outlines.
The [CFF page option](pdf-cff-page-hinting.md) connects CFF fitting to rendering.
The [Type 1 page option](pdf-type1-page-hinting.md) connects Type 1 fitting.
CFF and Type 1 remain unhinted by default; `--pdf-tt-hinting` applies only to TrueType.

The core uses isolated, licensed blue-zone and hint-map components with the
native font parsers and charstring interpreters. No external font library is required
at runtime. Source provenance, modifications and licenses are recorded under
`third_party/freetype-pshinter` and in `THIRD_PARTY_NOTICES.md`.

## Implemented profile

The current profile aligns horizontal stem edges through a piecewise-linear
vertical hint map. It handles blue-zone capture, family zones, overshoot control,
edge hints, active-mask changes, counter-mask ordering and the LanguageGroup
em-box behavior. Horizontal coordinates are scaled. Vertical stems retain their
positions in the mask but are not independently snapped on the X axis by this
profile. Stem darkening and synthetic bold are disabled.

The core accepts separate positive 16.16 X/Y scales in device pixels per font
unit. It emits line and cubic contours, quantized downward to 1/64-pixel device
coordinates. Input outlines, hint traces and PDF-independent advances are not
modified. Output widths retain the source values; fitted geometry does not
supply PDF text advances.

A glyph's initial hint map and active map are separate. Mask changes follow the
captured outline positions, including changes just before a contour closes.
Counter masks establish shared stem positions without replacing the active
mask. Empty and zero-length contours follow the component's path rules.

## Type 1 profile

Type 1 builds its initial map from all stem declarations, then replays declarations
and replacements in their original order. Three-stem groups retain their absolute
sidebearing-adjusted positions. Each composite component has a separate map and
a pre-fit origin; components share the caller's execution and fitting budget.

This profile uses the four blue-zone arrays, BlueScale, BlueShift, BlueFuzz and
LanguageGroup. Invalid or unresolved values in those fields reject fitting.
Stem-snap widths, ForceBold and ExpansionFactor are not consumed by this profile,
so unresolved values in those unused fields do not prevent fitting. This is not
an evaluator for computed PostScript definitions.

Open contours remain open for stroking. Their implicit fill closure is preserved
without adding a closing stroke. Logical current-point records do not add a path
vertex; subsequent absolute endpoints already include the position change.
The selected profile tolerates the known -20/-21 edge encodings on either axis,
while retaining input diagnostics. It does not implement legacy dotsection
suppression or flex flattening.

## Bounds and ownership

The internal entry points in `fonts/postscript/gridfit.h` are
`cupidimage_pdf_ps_gridfit` and `cupidimage_pdf_ps_gridfit_type1`.
Inputs are borrowed; the output must be a separate, empty result object.
The caller frees it with `cupidimage_pdf_type2_free`. The Type 1 font-level wrapper,
`cupidimage_pdf_type1_hint_outline`, also resolves bounded composite glyphs.

The current limits are:

- 96 stems and 65,536 events. For Type 1, the stem limit includes declarations
  across all replacement contexts, not just the active group.
- 65,536 input and output segments.
- Coordinates after component offsets, and private numeric parameters, within
  4,096 font units in magnitude.
- Positive axis scales no greater than 2 device pixels per font unit.
- Per-call array storage capped at 256 entries.

Invalid traces, nonfinite geometry and unsupported segment kinds are rejected.
The core fits one component at a time; a bounded wrapper resolves composite
base/accent glyphs with separate hint contexts. Empty masks and unused tail bits
retain diagnosed recovery. Private hint fields marked invalid are not used for
fitting. Admission and mask/segment processing consume a caller-owned work
budget. Allocation or budget exhaustion releases the context, arrays and partial
outline. The result is empty on failure. There is no shared mutable fitting state.

These are implementation limits, not PDF specification limits. Larger glyphs and transforms need further qualification. Component origins are
applied before fitting; checked affine transforms follow it.

## Verification

`make test-pdf-ps-gridfit` runs 190,754 authored checks covering ownership,
allocation failure, budget boundaries, malformed input, repeated contours and
2,000 bounded parameter/geometry stress cases. Input geometry must remain
unchanged. A 96-stem case exercises mask handling and buffer growth.

Optional independent checks use 22 pinned fonts and 23 authored CFF programs:

- 19,692 complete outline matches at six uniform sizes.
- 19,692 complete outline matches at six unequal-axis sizes.
- 6,564 exact grayscale glyph bitmap matches at two sizes, using the native
  coverage converter after fitting.

The authored programs cover blue/family/other zones, overshoot controls, ghost
and zero-width stems, changing masks, counter masks and LanguageGroup behavior.
The test also verifies that compact and 32-bit DICT integer encodings produce
the same native result.

A separate investigation found that the LP64 comparison parser misreads negative
DICT integers encoded with byte 29 as positive values before fixed-point
conversion. For example, a BlueValues delta of -20 loses its intended sign.
The native parser retains the signed value required by CFF. Independent authored
rendering comparisons use compact encodings to avoid conflating that parser
behavior with grid fitting; native encoding-equivalence checks retain the
32-bit form. The differing reference result is not counted as agreement.

### Type 1 checks

`make test-pdf-type1-gridfit` runs 31,899 native checks, including work-budget
boundaries, allocation failures, composite cleanup, malformed traces, open
contours, transforms and 2,000 bounded sizing/origin cases. It also runs optional
independent checks for 31 authored programs: 420 fitted fill-contour matches at
uniform sizes, 420 at unequal-axis sizes and 140 exact grayscale glyph bitmaps.

For installed PFB fonts, use:

    python3 scripts/test_pdf_type1_gridfit_reference.py bin/test_pdf_type1_gridfit --fonts /path/to/fonts
    python3 scripts/test_pdf_type1_gridfit_reference.py bin/test_pdf_type1_gridfit --fonts /path/to/fonts --nonuniform
    python3 scripts/test_pdf_type1_gridfit_reference.py bin/test_pdf_type1_gridfit --fonts /path/to/fonts --bitmaps

A local run added 75 installed PFB fonts to the authored cases. Each sizing sweep
matched 366,954 fitted fill contours, and the two-size bitmap sweep matched
122,318 glyph bitmaps. The bitmap checks passed with both tested comparison-engine
versions, 2.13.2 and 2.13.3. The installed fonts are not bundled or required by the
default suite.

The font comparison normalizes implicit fill closure. Separate native tests ensure
that an open contour does not acquire a closing stroke. These are standalone
glyph checks, not PDF page integration or complete Type 1 conformance.

`make test-pdf-type1` also checks 96 paired-font page cases for logical
current-point changes, covering all text modes, transforms, antialiasing and
Type 1 hinting on/off. The pairs also agree in 48 optional independent page checks.

## Remaining work

Designer counter priorities, computed private dictionaries, broader OtherSubrs,
flex policy, stem darkening, synthetic bold, fractional private parameters and
broader font-program arithmetic qualification remain open. The matching outlines
and bitmaps establish the tested profile, not complete CFF/PDF rendering parity.

The parameter and charstring rules come from Adobe [CFF, Technical Note 5176](https://pdfa.org/norm-refs/5176.CFF_.pdf)
and [Type 2, Technical Note 5177](https://pdfa.org/norm-refs/5177.type2_.pdf), alongside
the Type 1 font-format references listed in the hint-data document.
