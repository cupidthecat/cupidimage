# JBIG2 images in PDF

`JBIG2Decode` expands one-bit image XObjects and stencil masks before PDF Decode,
color conversion and image painting. The decoder is native and does not launch an
external renderer. Its rules come from ISO 32000-1 section 7.4.7, ISO 32000-2
section 7.4.7, and [ITU-T T.88 (08/2018), corresponding to ISO/IEC
14492:2019](https://www.itu.int/rec/T-REC-T.88-201808-I/en).

This is bitonal image compression. A JBIG2 symbol dictionary stores small bitmap
shapes; it is separate from a PDF font dictionary and does not provide Unicode
text, text selection or font substitution.

## Decoding path

The implementation covers:

- MQ arithmetic coding, integer and symbol-ID contexts, all fifteen standard
  Huffman tables, transmitted Huffman tables and run-coded symbol-ID tables.
- Generic arithmetic regions with four templates, adaptive pixels, typical-row
  prediction, skip masks and the extended twelve-pixel template.
- Generic refinement with both templates, adaptive pixels, reference offsets and
  typical-pixel prediction.
- Arithmetic and Huffman symbol dictionaries, raw or MMR collective bitmaps,
  imported and exported symbols, retained bitmap statistics, single-symbol
  refinements and nested symbol aggregation.
- Text-region strips, four reference corners, transposed placement, spacing
  offsets, bitmap combination operators and refined instances.
- Pattern dictionaries and halftone regions, including Gray-coded bitplanes,
  fixed-point grid placement, skip masks, MMR planes and clipped patterns.
- Embedded segment graphs, global segments, page buffers, intermediate regions,
  page refinement, stripe boundaries and unknown-length generic regions.

The image stream and its `JBIG2Globals` stream are resolved separately. Each keeps
its own object identity for decryption. General-purpose byte filters can precede
JBIG2 decoding or compress the globals stream. Recursive image decoding is not
used to resolve globals. Filter names and DecodeParms may be indirect; unrelated
parameter keys are ignored.

Internally, bitmaps use black=1. The filter converts them to PDF sample polarity,
where zero is black and one is white. PDF Decode arrays then apply normally,
including reversed ranges and stencil-mask painting. Row padding does not become
image pixels. Inline images cannot use this filter.

## Bounds and invalid input

A decode has a 128 MiB tracked allocation limit, a 64 MiB combined encoded-input
limit and a 500-million-unit work budget. Page and intermediate bitmap dimensions are limited to 65,535
pixels per axis and 64 million pixels in total. Dictionaries and segment lists
are limited to 65,536 entries; references across the segment graph are limited to
1,048,576. Text and aggregation procedures accept at most 1,048,576 instances.
These are resource limits, not limits imposed by the PDF specification.

Huffman prefixes and ranges may contain up to 255 bits. Wide codes are processed
without shifting a machine word past its width; decoded integer values must fit
the signed range required by their procedure. Segment references must point to
lower-numbered, available segments. Duplicate numbers, invalid reference types,
missing globals, inconsistent dimensions, missing arithmetic termination markers and
exhausted budgets return errors.
Allocations remain owned by the decode until explicitly released or transferred
to the caller.

MMR decoding shares the fax bit reader, with a separate bounded entry point.
JBIG2 does not permit the fax uncompressed extension. Halftone planes and
unknown-length generic regions require EOFB; known-length collective bitmaps may
omit it. Existing PDF fax and TIFF callers retain their previous interface.

Some documents place a page-local dictionary before the page-information segment.
The decoder accepts that ordering, but requires page information before painting
regions. It also accepts single-page sequential or random-access T.88 files when
producers leave their standalone header in a PDF image stream, even though PDF
requires the embedded organization. Unused Huffman aggregation-table selectors
are resolved without changing non-aggregate symbol decoding. Non-necessary extension segments do not alter the page. Unsupported
necessary extensions fail rather than returning an incomplete image. Colour
extensions are outside PDF's one-bit JBIG2 filter and are not accepted.

## Tests

Run `make test-pdf-jbig2` for the focused suite. It includes independent MQ encoding
controls, exact packed-bitmaps, all standard Huffman tables, transmitted tables,
long code lengths, segment headers, reference graphs, truncation, allocation
failure and bounded input mutation. Ordinary tests do not need another decoder.

The tests compare compressed PDF images with raw-image controls, including image
masks, reversed Decode arrays, filter chains, indirect controls, shared globals
and invalid inline images. `assets/pdf/native-jbig2.pdf` combines generic regions,
arithmetic and Huffman text regions, and a halftone region. Its stored bitmap
controls are owned test patterns, not third-party document images.

The bitmap scripts accept an optional `--reference-adapter` for separate decoder
comparisons. Those comparisons are exact, not tolerance-based. Some independent
decoders disagree on extended adaptive pixels, single-pattern halftones, default
halftone backgrounds or clipped grids. The halftone script has explicit subset
options for investigating those differences; excluded cases are still checked
against the owned bitmap controls and are not counted as independent matches.

`make test-pdf-jbig2-corpus` downloads 99 hash-pinned PDF.js documents if they are
not already cached. It checks first-page loading and decoded image samples
separately. The sample hashes were compared exactly with an independent decoder.
A separate raster-extraction profile agreed on 81 of those 99 samples; its 18
disagreements remain recorded as differences, including typical refinement,
halftone skip/background handling and several Huffman cases. These are not
converted to tolerance-based passes.

The broader PDF image pipeline still has unfinished resampling, color-management
and rendering-profile work. Successful JBIG2 decoding does not establish full
PDF rendering parity. See [PDF images](pdf-images.md) and the
[feature inventory](pdf-feature-matrix.md) for the wider status.
