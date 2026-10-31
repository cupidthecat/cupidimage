# PostScript flex hints

The CFF hinting option retains and applies Type 2 `flex`, `hflex`, `hflex1` and
`flex1` instructions. Their two cubic segments remain unchanged in the unhinted
outline. A separate hint record stores the first segment and depth threshold;
subroutine calls preserve the same record positions.

When CFF hinting is enabled, a shallow pair becomes a line if its device-space
depth is below the threshold. Equality retains the curves. `flex` supplies the
threshold in hundredths of a device pixel; the other three operators use 50.
Zero or negative thresholds retain the curves.

Depth is the perpendicular distance from the joining endpoint to the chord
between the outer endpoints. The page adapter supplies the full linear
character-to-device transform, so rotations, reflections, shear and unequal
scales participate in that measurement. Translation and component origins
cancel. Standalone fitting uses its diagonal scales unless an explicit depth
transform is supplied. A zero-length device chord retains the pair.

The decision precedes blue-zone fitting. A collapsed pair passes its final
endpoint through the ordinary line-fitting path. PDF text widths do not change.
Fill, stroke and text clipping all consume the resulting hinted outline.

The operator and threshold rules are specified in Adobe's
[Type 2 Charstring Format, Technical Note #5177](https://adobe-type-tools.github.io/font-tech-notes/pdfs/5177.Type2.pdf),
section 4.1. Type 1 uses the separate procedure below.

The owned sample `assets/pdf/native-cff-flex.pdf` pairs flex and literal-curve
glyphs at four sizes. Regenerate it with `scripts/generate_pdf_flex_fixture.py`.
Its complete page is decoded-unverified.

## Bounds and verification

Flex records are separately owned and released with the hint program. A program
can retain at most 32,768 records; the existing 65,536-segment and charstring-work
limits still apply. Fitting validates adjacent cubic pairs, sorted nonoverlapping
records, finite thresholds and transforms. It rejects an intervening hintmask
inside a pair, which cannot be emitted by a single flex operator.

`make test-pdf-flex` covers transform and threshold boundaries, retained metadata,
subroutines, unhinted geometry, explicit line/curve controls, PDF paint and clip
modes, allocation failure and budget exhaustion. The grid-fitting suite also
checks invalid records, overlapping pairs and failed output ownership.

Optional comparisons are deliberately separate:

```sh
python3 scripts/compare_pdf_flex.py bin/test_pdf_ps_gridfit
```

The tested engine version 2.13.2 retains the curves at small sizes: eight authored
outlines match and twelve differ for that documented policy. Those differences
are reported, not accepted as pixel matches. The broader existing uniform-size
sweeps still have 19,740 matching outlines each for uniform and unequal-axis
sizing. A separate bitmap run matches 6,580 grayscale glyphs. These results do
not establish full CFF or PDF rendering parity.

## Type 1 flex

Standard OtherSubrs calls retain the reference point as well as the threshold and
returned endpoint. The Type 1 fitter uses the distinct selection and adjustment
rules in [Adobe Type 1 Font Format](https://adobe-type-tools.github.io/font-tech-notes/pdfs/T1_SPEC.pdf),
section 8.3 and Appendix 3. It does not reuse the Type 2 depth calculation.

The procedure selects an aligned device axis, compares the rounded character-space
height after transformation, and either shrinks the pair or adjusts the retained
join and its neighboring controls to the pixel phase. Arbitrarily rotated pairs
retain their curves. The specified alignment tolerances and positive-tie rounding
are retained; erosion defaults to 1 in this fitting profile. The low-level helper
also tests explicit stroked and non-eroded configurations.

Embedded PaintType is ignored, as required by ISO 32000-1:2008, 9.9 and the
corresponding PDF 2.0 clause. PDF text rendering modes still choose fill, stroke
and clipping. Tests compare PaintType 0 and 2 programs in every text mode.
Component translation participates in pixel-phase adjustment, while PDF advances
and the charstring's logical current point remain unchanged.

`make test-pdf-type1-flex` checks literal collapsed and adjusted outlines, retained
reference points, page transforms, clipping, unchanged unhinted geometry and
allocation failure. The core includes 8,192 bounded phase/transform cases. The
Type 1 fitting suite checks malformed event pairs and failed-output ownership.

The targeted optional comparison is `scripts/compare_pdf_type1_flex.py`. Engine
2.13.2 matches one tested outline and retains unadjusted curves in six others.
Those six are reported as differences. The separate existing Type 1 outline and
bitmap sweeps cover ordinary fitting, not all flex behavior. General OtherSubrs
execution, custom device erosion policies and complete Type 1 fidelity are not
established by this selected standard-call profile.
