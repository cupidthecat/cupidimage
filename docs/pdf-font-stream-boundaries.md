# PDF font stream boundary recovery

Some reduced and damaged PDFs contain usable font programs but incorrect wrapper
boundaries. The reader has two limited recovery paths for these files. Neither
changes the PDF lexical rules or treats malformed files as conforming documents.

## Adjacent scalar terminators

An indexed, uncompressed indirect object may contain a PDF number immediately
followed by `endobj`, without a separator. The reader accepts that one numeric
value when the exact terminator follows it. The object header must still match
the selected number and generation. Numeric token length, finite-value checks and
the object work budget still apply.

This does not split arbitrary keywords or content-stream tokens. Joined Boolean
values, exponent notation, malformed numbers and an `endobj` prefix inside a
longer word are not recovered. An indirect stream length must still be a valid
nonnegative integer, and the resulting stream boundary must match `endstream`.
The reader does not search the file for a different object or guess a byte length.

The ordinary number parser and the headerless test-fixture scanner retain their
existing contracts. This repair runs only at the indexed object boundary.

## Encrypted Type 1 segments

The PDF font wrapper supplies Length1, Length2 and Length3. The reader still
requires numeric, nonnegative integral values. It checks the clear-text program
and its `currentfile eexec` boundary rather than trusting an inconsistent offset.
Correct offsets retain their exact binary boundary, including leading whitespace
bytes inside the encrypted data.

If an offset is inconsistent, the reader uses the parsed boundary and decrypts
only bytes from that decoded font stream. If a declared encrypted extent cuts
through a font declaration, parsing can continue within the same stream. Recovery
requires a complete private program ending with an unshadowed `currentfile
closefile` sequence. Strings, procedures and shadowed operators cannot supply
that terminator. A correct declared boundary retains the existing implicit-end
behavior; standalone Type 1 callers keep their explicit-length contract.

The parser uses one clear-text pass and one private-program pass. It does not
retry allocation failures, reset the syntax work budget or try arbitrary cipher
offsets. The 16 MiB font limit and 1,048,576-unit syntax budget remain in place.
The retained private allocation is included in the font-table memory budget.
Truncated programs, ambiguous cipher starts, missing glyph dictionaries and
unsupported computed programs still fail. Recovery never substitutes a different
font for the supplied program.

## Tests

`make test-pdf-font-boundaries` compares owned repaired wrappers with correctly
written controls. Cases cover direct and indirect segment lengths, filtered and
unfiltered streams, signed and fractional scalar objects, explicit font widths,
hinting/coverage options, malformed numbers and incomplete private programs.
Parser and page allocation failures check cleanup without fallback retries.

The owned `assets/pdf/recovered-font-streams.pdf` combines an incorrect Type 1
clear-text length with adjacent scalar terminators. It draws two geometric glyphs.
Regenerate it with `scripts/generate_pdf_font_boundaries_fixture.py`. Its complete
raster remains decoded-unverified; the paired page tests provide the focused
evidence.

The syntax and stream rules follow ISO 32000-1 sections 7.2, 7.3.8 and 7.3.10;
embedded font stream lengths are defined in section 9.9. Type 1 program boundaries
follow chapters 2 and 7 of Adobe's
[Type 1 Font Format](https://pdfa.org/norm-refs/Type1Fonts.pdf) and the `eexec`
operator contract in the PostScript Language Reference. These sources define the
valid structures. The recovery policy above is deliberately narrower than a
general PostScript interpreter or damaged-file reconstruction.
