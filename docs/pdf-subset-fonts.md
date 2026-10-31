# Embedded CID subset-font recovery

Three narrowly defined recovery paths help display damaged embedded CID fonts.
They do not make the damaged font programs conforming, substitute different
outlines, or relax the normal font parser entry points. Simple-font loading
retains its existing validation policy.

## TrueType glyph locations

The OpenType [loca definition](https://learn.microsoft.com/en-us/typography/opentype/spec/loca)
requires ascending glyph offsets. Equal adjacent offsets describe an empty glyph.
Some embedded CID subsets instead contain zero resets or out-of-order locations.

PDF CID loading first tries the ordinary initializer. If only location ordering
prevents admission, the recovery initializer retains the existing header,
metric and table-boundary checks, then builds an owned glyph-end index:

- Equal adjacent offsets remain empty glyphs.
- A nonempty span ends at the next greater recorded offset, or the end of glyf
  when no greater offset exists.
- The glyph decoder still validates the entire simple or composite description
  within that span. It cannot borrow bytes across another recorded glyph start.

The original font bytes and glyph IDs are unchanged. CIDToGIDMap still selects
which glyph is drawn; PDF widths and vertical metrics still control text advances.
Glyph instructions, composite references and coordinates keep their existing
validation and work limits.

The index is built once, rather than searching the location table on every glyph
load. It costs four bytes per glyph in the existing 64 MiB font-program budget.
Construction also uses a temporary four-byte array per location. Hinted and
unhinted loading share the same span accessor. The font program owns the index,
and prepared-font cache entries retain that owner until they are released.

The ordinary TrueType initializer continues to reject unordered locations.
Out-of-range offsets, truncated location/metric tables and invalid glyph
payloads are not repaired. This recovery does not reconstruct missing font data
or promise to recover arbitrary overlapping descriptions.

## Missing TrueType zone declaration

OpenType's [maxp table](https://learn.microsoft.com/en-us/typography/opentype/spec/maxp)
requires maxZones to be 1 or 2. Some embedded CID subsets instead set both
maxZones and maxTwilightPoints to zero, even though a preparation program remains.
The PDF CID loader marks this specific case for compatibility. The hinting
initializer can then prepare the implicit glyph zone without inventing twilight
points. Normal standalone and simple-font hinting still reject maxZones = 0.

The original font bytes are unchanged. A nonzero twilight count does not qualify
for this recovery, and maxZones values above 2 remain invalid. Instruction access
keeps its normal bounds: a program that accesses a missing twilight point fails.
The flag belongs to the retained font program and is copied into its prepared
hinting state. It neither allocates extra point storage nor changes PDF widths,
vertical origins or character selection. It also composes with location recovery.

## CFF font-dictionary selection

Adobe [Technical Note 5176, section 19](https://adobe-type-tools.github.io/font-tech-notes/pdfs/5176.CFF.pdf)
requires a format-3 FDSelect table to start at glyph zero. A damaged CID subset
may start at glyph one, leaving .notdef without a font-dictionary selection.

PDF CID loading accepts that omission only when FDArray contains exactly one
font dictionary. The missing glyph-zero selection then has only one possible
value. Larger leading gaps, multiple dictionaries, invalid indexes, bad ranges
and incorrect sentinels remain errors. Other glyph selections and charstrings
are unchanged. The ordinary CFF initializer still rejects the missing range.

ISO 32000-1 section 9.9 defines the embedding requirements. The exceptions above
are explicit compatibility behavior, separate from those normative formats.

## Tests and qualification

Run `make test-pdf-subset-fonts`. The structural suite checks recovery against
valid authored controls, retained strict rejection, empty glyphs, composites,
truncated tables, cross-record reads and allocation failure. Page tests cover all
eight text modes, horizontal and vertical CID text, explicit CIDToGIDMap,
all five rendering options, and Form-local font/cache lifetimes.

The two owned reference assets are deliberately simple blue rectangles:
subset-cid-truetype.pdf has reordered locations and a zero tail;
subset-cid-cff.pdf omits the glyph-zero selector. Both match literal pixel
expectations and independent 96-DPI rasters exactly. Rebuild them with
`scripts/generate_pdf_subset_fixtures.py`.

For the original damaged TrueType subset, all 4,897 mapped glyph IDs were checked
at six sizes in monochrome and grayscale instruction profiles. All 58,764 outline
comparisons matched the independent legacy instruction profile. Of those glyph
IDs, 275 have nonempty outlines; the comparison count includes empty glyphs.
This checks the recovered glyph loading, not complete PDF raster fidelity.

Both original corpus pages now render with all five PDF options enabled. Their
whole-page comparisons still differ: bug1146106.pdf has mean absolute channel
error 0.20173 and maximum 233; bug1650302_reduced.pdf has mean error 4.81602 and
maximum 210. These are recovered loads with remaining pixel differences, not
completed font-fidelity requirements. The original documents and embedded font
bytes are not redistributed.

See [PDF fonts](pdf-fonts.md), [native TrueType loading](pdf-truetype-loader.md)
and [the feature inventory](pdf-feature-matrix.md) for the broader work.

### Zone-declaration checks

`make test-pdf-cid-zone-recovery` compares owned zero-zone and valid controls across
horizontal/vertical CID text, all text modes, hinted/unhinted outlines, grayscale
coverage and reordered locations. It includes invalid-zone and twilight-access
cases, strict simple-font rejection, Form-local cache lifetimes and allocation
failures: 273 page checks and 645 allocation checks. Optional owned-font comparisons
add 48 exact outline checks. The owned `assets/pdf/cid-truetype-zero-zones.pdf` has a whole-page control.
The loader unit suite exercises all sizes from 1 through 96 in both instruction
profiles, preparation scan controls, allocation failure and retained strict limits.

All three pages of the pinned [vertical-text document](https://github.com/mozilla/pdf.js/blob/67667f1e449378a2675f4095cded012b7ce5f37b/test/pdfs/vertical.pdf) now render with all five
PDF options enabled. Its embedded subset contains 3,723 glyph IDs, only eight with
nonempty outlines. At six sizes and in both monochrome and grayscale profiles,
all 44,676 native outline comparisons match the independent legacy instruction
profile in both tested engine versions, 2.13.2 and 2.14.3. The count includes empty glyphs. Original document and font bytes are
not redistributed, and successful loading does not establish whole-page fidelity.

The original page comparisons still differ at 96 DPI: mean absolute channel error
is 1.38435 for page 1 and 1.15854 for pages 2 and 3, with maximum error 251. These
are recovered pages with remaining raster differences, not full-page matches.
