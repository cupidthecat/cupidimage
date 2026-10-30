# Type 1 counter-control data

The Type 1 interpreter accepts the standard counter-data calls to OtherSubrs 12
and 13. These instructions previously caused otherwise drawable glyphs to fail.
They now preserve unhinted geometry and work with the selected Type 1 page-hinting
profile. Designer-specified counter priorities are retained but not applied.
This is compatibility support, not complete counter-control hinting.

## Decoding and traces

A counter-data block follows `hsbw` or `sbw`, before drawing or other hints.
Calls to entry 12 transfer up to 22 operands at a time; one call to entry 13
finishes the block. Subroutine calls and division may supply the operands.
The decoder preserves the specified pseudo-stack order, including blocks split
across multiple calls. A second block or an unfinished sequence is rejected.

The completed data contains horizontal and vertical group lists. Each group has
delta/width pairs; a negative width identifies its final pair. Trace events retain
the encoded delta, signed width, orientation, group priority and final-pair flag.
Unlike ordinary stem events, these values are not converted to absolute stem
coordinates. The trace also records that counter data was present, even when
both group lists are empty.

The fitting profile still builds its map from ordinary stem declarations. Counter
events do not become replacement hints or alter charstring coordinates. This
preserves the format's backward-compatible drawing behavior while the full
counter-allocation policy remains unfinished.

## Bounds and ownership

Storage is limited to 65,536 counter operands per glyph. Transfers, buffer growth
and final parsing consume the shared work budget and the glyph's local budget.
Nonfinite values, excessive magnitudes, invalid group counts, truncated groups,
extra trailing data and misplaced calls fail explicitly. Partial counter buffers,
traces and outlines are released on failure. Trace capture is optional; ordinary
outline decoding still validates the counter-data structure.

## Checks

`make test-pdf-type1` includes:

- 90 program/trace cases covering chunk sizes 1 through 22, encryption settings,
  sidebearings, fractional operands, subroutines, empty lists and malformed sequences.
- 96 paired page cases across text modes, transforms, antialiasing and hinting.
- 200 page allocation checks, including malformed sequences.
- 7,140 native budget, ownership and boundary checks, including 2,000 deterministic
  malformed-input mutations.

An optional run adds 48 independent page-pair comparisons:

    python3 scripts/test_pdf_type1_counters.py bin/test_pdf_type1 bin/test_pdf_pages bin/test_pdf_clip_oom --reference

These compare the drawing with and without counter metadata; they do not test
application of designer priorities. The owned `assets/pdf/type1-counter-hints.pdf`
contains two horizontal stems and a matching counter group. Regenerate it with
`scripts/generate_pdf_type1_counter_fixture.py`. Its full raster remains
decoded-unverified.

The calling convention and data layout follow Adobe
[Type 1 Font Format Supplement, Technical Note 5015, sections 2.2 through 2.4](https://adobe-type-tools.github.io/font-tech-notes/pdfs/5015.Type1_Supp.pdf).
That document also describes compatibility behavior for rasterizers without
direct support for the supplied counter hints. Multiple-master interpolation and
custom OtherSubrs procedures remain unsupported.
