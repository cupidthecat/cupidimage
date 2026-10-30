# PDF objects and revisions

PDF rendering depends on selecting the right objects before interpreting fonts,
images or drawing commands. An earlier physical definition is not necessarily the
active one, and an object number alone does not identify an indirect reference.

## Cross-reference index

The reader builds a bounded index from classic cross-reference tables, cross-reference
streams, or hybrid-reference files. It follows `Prev` from the latest section,
keeps the newest definition, and applies the current classic table before its
`XRefStm` supplement. Auxiliary streams do not follow their own `Prev` entries.
Multiple table subsections and stream `W`, `Index` and `Size` fields are checked.
Omitted field widths use the specified defaults; wide fields may contain leading
zero bytes, but overflowing known values are rejected.

References retain both object number and generation. Free entries, undefined
numbers, unknown cross-reference entry types and generation mismatches resolve
to null rather than reviving older physical objects. A malformed indexed object
or an allocation failure is an error, not an absent value. Required entries still
have to satisfy their expected types. This does not mean every optional rendering
parameter has completed its null/default-value audit.

The latest trailer supplies the catalog reference. Stream and table offsets must
identify matching object headers. Complete PDF files do not fall back to physical
object scanning when their index is invalid. Headerless parser fixtures retain a
separate scanning path for focused internal tests.

## Compressed objects and streams

Compressed objects are selected by the declared object-stream number and header
index. The reader checks the object number at that index, the header extent,
ordered body offsets, generation and container type. It does not search other
object streams for a similarly numbered object. Object-stream dictionary values
can refer to uncompressed objects, with bounded reference-chain resolution.

Stream boundaries use the resolved `Length`, followed by `endstream` and `endobj`.
Filter names, filter arrays, decode-parameter dictionaries and their controls can
be indirect. Escaped names are decoded once. Unknown decode-parameter keys are
ignored, and null controls retain their defaults. Filters in cross-reference
streams must remain direct because they are needed to construct the index.

Legacy indirect cross-reference-stream lengths are bootstrapped from the exact
decoded row count. The reader then resolves the declared length against that
revision's index, validates the real boundary and compares every decoded byte.
Candidate delimiters alone never establish a valid stream. This compatibility
path is not a PDF 2.0 writer-conformance check.

Page `Contents` accepts a stream or an indirect/direct array of stream references,
including reference chains. An absent or null `Contents` paints an empty page;
malformed arrays and reference cycles fail. Decoded streams are joined with a
newline before interpretation.

## Limits and tests

- At most 1,048,576 cross-reference rows across 128 sections.
- At most 64 MiB of decoded cross-reference data across those sections.
- At most 32 nested object-resolution steps and a 64 MiB work budget per lookup.
- At most 128 candidate legacy stream boundaries and 64 MiB of candidate input.
- At most eight byte filters and 64 KiB of normalized filter parameters.
- At most 128 page content streams and 64 MiB of combined decoded page content.

`make test-pdf-xref` generates complete PDFs for revision precedence, generations,
free entries, compressed-object indices, filter chains, predictors, indirect
parameters, content arrays and malformed inputs. It also injects allocation
failures through the complete page-loading path and checks for leaks or changed
pixels on successful recovery. These tests establish object-resolution behavior,
not complete PDF rendering or visual equivalence across the broader corpus.

The relevant rules are in [ISO 32000-1, sections 7.3.9–7.3.10 and
7.5.4–7.5.8](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf)
and the [PDF 2.0 clause 7 errata](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html).
[RFC 7995](https://www.rfc-editor.org/info/rfc7995/) defines a particular PDF output
profile for RFCs, not a replacement for the general object and rendering rules.
Encryption, external stream files and damaged-file reconstruction remain outside
this implementation. See the [conformance tracker](format-conformance.md) for the
remaining rendering work.

## Flate stream endings

PDF Flate streams accept a single extra LF, CR or CRLF after the complete zlib
payload, as permitted by ISO 32000-1 section 7.3.8.2. The decoder first locates
the checksum boundary using RFC 1950 framing, then validates the remaining
bytes. Arbitrary garbage, repeated line endings and concatenated zlib streams
are still rejected. Checksum and truncation checks remain mandatory.

This rule applies to PDF stream decoding, including embedded font programs,
content and image data. It does not relax the strict whole-stream entry point
used by the other formats. `make test-pdf-flate-extent` compares TrueType, CFF,
Type 1, content, image and chained-filter cases with their unpadded equivalents,
and checks allocation-failure cleanup.

## Decoded names

The shared name reader accepts the empty name `/` and up to 127 decoded bytes.
Escaped bytes are decoded before checking that limit; an encoded zero byte remains
invalid. Resource keys, font selections, Type 3 glyph names and colorant names use
these decoded values. A valid name token is not necessarily a valid value for a
particular entry: Encoding and BaseEncoding still require a supported encoding.
See ISO 32000-1 section 7.3.5 and Annex C.
