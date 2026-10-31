# Modern TrueType fitting

Select `--pdf-tt-modern-hinting --pdf-text-aa` for the modern grayscale instruction
profile. The API uses `truetype_hinting = CUPIDIMAGE_PDF_TT_HINT_MODERN` and
`text_antialias = 1`. Without antialiasing, the profile starts with full
monochrome movement but still advertises its own instruction capabilities.
Glyph instructions can change compatibility control through INSTCTRL. Existing legacy
and unhinted options retain their meanings.

## Instruction behavior

The profile advertises interpreter version 40 and grayscale-compatible subpixel
capabilities through GETINFO. It suppresses horizontal point movement while
retaining touch flags and the permitted vertical adjustments. SHPIX and DELTAP
respect touched-point, composite and twilight rules. Completing both IUP axes
ends later compatibility-mode adjustments; flag-changing instructions still
consume their checked stack operands. INSTCTRL can select full instruction
control. These changes do not relax stack, point, arithmetic or work limits.

Composite offset rounding follows the selected movement policy. Compatibility
mode preserves the unrounded phantom geometry, and vertical phantom X coordinates
use half the horizontal advance. PDF Widths, CID vertical metrics, spacing and
text matrices still determine layout. Font-program advances never replace PDF
text advancement.

This is a renderer policy, not an additional PDF requirement. The instruction
rules come from the [OpenType instruction specification](https://learn.microsoft.com/en-us/typography/opentype/spec/tt_instructions),
with the selected grayscale policy described in the official
[font-engine profile guide](https://freetype.org/freetype2/docs/hinting/subpixel-hinting.html).
PDF text layout follows ISO 32000. The profile does not produce LCD color-channel
coverage or add variable-font instance support.

## Fonts requiring full control

Some fonts require both axes to remain under instruction control. A bounded
classifier reads family names and known cvt/fpgm/prep signature combinations.
It uses the WWS, typographic and legacy family-name order, platform/language
selection and subset-prefix handling. Optional malformed name records are
ignored within their table bounds. Signature checksums are computed from the
actual table bytes, not trusted from the SFNT directory.

The compatibility identifiers contain no font programs. Their source, license
and hashes are retained in `third_party/freetype-tt-policy` and the third-party
notices. Name scanning and checksum work consume the preparation budget. The
classifier allocates no additional font metadata storage.

## Cache and output paths

Prepared font state includes the profile in its cache key. Switching between
legacy and modern fitting cannot reuse incompatible preparation state. Returned
outlines retain their own ownership across cache replacement. The setting passes
through the page API, command line, Forms, patterns and soft masks. Review reports
record modern fitting as value 2; previous Boolean option profiles keep their
existing hashes.

## Verification and limits

The VM suite checks capability selectors, render modes, program stages,
full-control overrides and point movement. Owned page controls compare modern
horizontal suppression with a font that contains only the vertical instructions,
including transforms, all eight text rendering modes, CID horizontal/vertical
placement, PDF spacing, allocation failures and Sixel output. Metadata tests cover
family selection and every retained signature triplet, including changed table
bytes with unchanged directory checksums.

Optional independent tests compare authored instruction outlines and transformed
grayscale bitmaps. The selected composite full-control rounding policy follows
engine version 2.14.3: older 2.13 engines suppress X-offset rounding in that case,
so those 72 comparisons are reported as skipped rather than matches. They are separate from complete-document comparisons and do
not establish complete PDF fidelity. Broader font/corpus qualification, LCD
layouts and variable-font behavior remain outside this profile's verified scope.
