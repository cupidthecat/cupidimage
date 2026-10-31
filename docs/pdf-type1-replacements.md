# Type 1 hint replacement traces

Type 1 fonts can replace their stem hints while drawing a glyph. The fitting
wrapper no longer rejects a glyph merely because the combined declarations
across those groups exceed 96. The native charstring interpreter still records
the declaration order and each replacement's position in the outline.

The selected fitting profile builds an initial map from all declarations, then
replays the active groups. Its map has room for 96 hints. If a set exceeds that
capacity, the map component uses its defined unhinted mapping behavior instead
of discarding the glyph. This is a fitting-profile behavior, not a 96-hint limit
in the Type 1 language. It does not deduplicate, reorder or invent stem hints.
CFF's separate 96-hint validation remains unchanged.

The wrapper retains at most 97 entries for a map: 96 usable entries and one that
marks overflow. Further entries cannot affect a map that has already exceeded
capacity. Every event is still validated, including coordinates in declarations
after the overflow. Replacements clear the active storage normally. Work charges
use the largest active group rather than charging every replacement for the
entire accumulated trace. The 65,536-event limit and per-glyph page budget remain
in force; neither allocation nor execution becomes unbounded.

## Verification

`make test-pdf-type1-replacements` checks owned fonts with 96, 97, 100, 128, 256
and 320 declarations, including horizontal and vertical hints and replacements
between path segments. The 1,420 page checks cover painting modes, clipping, transforms,
antialiasing and PDF advances, alongside 222 allocation-failure checks. The owned
`assets/pdf/type1-hint-replacements.pdf` compares six longer traces with literal
outline controls, and its complete page is checked with hinting both on and off.

The fitting unit test also checks 1,024 and 65,536 declarations, invalid late declarations,
allocation failure and the existing work-budget boundaries. Optional independent
checks compare fitted outlines and grayscale masks. They do not establish full
PDF page fidelity or complete Type 1 support.

The governing language rules are in section 8.1 of the
[Adobe Type 1 Font Format](https://adobe-type-tools.github.io/font-tech-notes/pdfs/T1_SPEC.pdf).
See the [grid-fitting guide](pdf-postscript-gridfit.md) for the selected profile,
its licensed component provenance and the remaining fitting limits.

A focused installed-font run checked 87 PFB fonts alongside 47 owned programs:
389,658 fitted outlines at uniform sizes, 389,658 at unequal-axis sizes and
129,886 grayscale masks matched the independent 2.13.2 engine. The same 129,886
masks also matched version 2.14.3. These are standalone glyph comparisons, not
ratings of PDF documents. The installed fonts are not redistributed with the tests.
