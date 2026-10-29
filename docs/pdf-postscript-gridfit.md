# CFF outline grid fitting

A bounded CFF grid-fitting core consumes the [retained hint data](pdf-postscript-hints.md)
and returns an owned device-space outline. The [CFF page option](pdf-cff-page-hinting.md)
connects it to rendering. CFF and Type 1 remain unhinted by default;
`--pdf-tt-hinting` alone does not enable CFF fitting.

The core uses isolated, licensed blue-zone and hint-map components with the
native CFF parser and Type 2 interpreter. No external font library is required
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

## Bounds and ownership

The internal entry point is `cupidimage_pdf_ps_gridfit` in
`fonts/postscript/gridfit.h`. Inputs are borrowed; the output must be a separate,
empty result object. The caller frees it with `cupidimage_pdf_type2_free`.

The current limits are:

- 96 stems and 65,536 mask events.
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

## Remaining work

Type 1 input, flex policy, stem darkening, synthetic bold, fractional private
parameters and broader font-program arithmetic qualification remain open. The matching outlines
and bitmaps establish the tested profile, not complete CFF/PDF rendering parity.

The parameter and charstring rules come from Adobe [CFF, Technical Note 5176](https://pdfa.org/norm-refs/5176.CFF_.pdf)
and [Type 2, Technical Note 5177](https://pdfa.org/norm-refs/5177.type2_.pdf), alongside
the Type 1 font-format references listed in the hint-data document.
