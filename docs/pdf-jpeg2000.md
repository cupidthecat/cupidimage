# PDF JPEG 2000 images

JPXDecode image XObjects retain native component samples until PDF color and mask
processing. They do not pass through an 8-bit RGB image first. The decoder is
compiled into the library, with no system codec dependency or subprocess at
runtime. This is partial JPX support, not complete JPEG 2000 or PDF conformance.

## Samples and containers

The image path accepts raw codestreams and the supported JP2/JPX container subset.
It preserves signed sample metadata, component grids and precision. Owned tests
cover lossless 5/3 and irreversible 9/7 wavelets, multiple resolutions, tiles,
quality layers, five progression orders and component transforms. A sample-plane
comparison checks the decoded values before page painting can hide an error.

The container reader handles bounded boxes, image headers, per-component depths,
palettes, component maps, channel definitions and color specifications. Palette
entries retain up to 38 bits, independently of the codestream sample precision.
Color channels follow their declared associations; four ordinary channels are
not automatically treated as RGB plus alpha. The first codestream and layer can
use identity registration. In-file fragment tables and shared header fragments
are bounded and owned until painting finishes. External references are rejected
without opening files or making network requests.

Width and Height must agree with the decoded image. BitsPerComponent is ignored
for JPX images. An explicit PDF ColorSpace takes precedence over embedded color
specifications. Otherwise, the renderer selects a supported specification by
precedence and approximation, with the device-space fallback required by
ISO 32000-2 section 7.4.9. Decode is ignored when ColorSpace is absent, except for
stencils. With an explicit ColorSpace, Decode applies to native samples.
JPXDecode can follow supported byte filters but must be the final stage in the
current implementation. Inline JPX images are rejected.

## Color and opacity

Embedded sRGB, sRGB gray, sYCC, ROMM RGB and CMYK have native color paths. CIELab
supports default ranges and explicit range/offset values with D50 or D65.
The sYCC path retains extended-gamut RGB through its symmetric transfer curve
before output conversion. ROMM RGB includes its linear toe. These image source
spaces do not become additional transparency-group blending spaces.

Embedded restricted and unrestricted ICC profiles use the existing supported
matrix/TRC, LUT and multi-stage A-to-B paths. Profiles are copied into the
page's bounded ICC store and reused by content identity. An unsupported profile
can fall through to another color specification; an allocation or work-budget
failure cannot silently select different colors. Complete ICC and rendering-intent
coverage remains unfinished. Signed encoded values retain their zero point when
mapped into embedded RGB/ICC spaces.

SMaskInData 0 ignores encoded alpha. Values 1 and 2 require one unsigned opacity
channel associated with every ordinary color channel. They override Mask and the
current graphics-state soft mask, and cannot accompany SMask. Value 2 recovers
premultiplied samples before Decode and palette lookup. Interpolation weights
recovered colors by alpha so undefined transparent samples do not bleed into
neighbors. Separate JPX grayscale soft masks, color-key masks, and one-bit JPX
stencils use the same image painting path.

## Bounds and malformed input

Encoded input is limited to 64 MiB. Decoded codestream planes share a 16-million-
sample limit and a 64 MiB storage limit. The adapter accepts up to 64 components
and codestream precisions from 1 through 31 bits, subject to the decoder's
code-block magnitude limits. It does not cover all PDF-permitted 38-bit
codestreams. Codec allocations have a separate 128 MiB limit, with alignment,
allocation metadata and transient reallocations included in the accounting.

Reads, allocations, packets and code blocks consume the page's shared work
budget. Metadata has box, nesting, fragment and ownership limits. Codec warnings
are treated as failures rather than allowing an unsupported transform to be
silently skipped. Allocation-failure tests check cleanup throughout the pipeline.

A tile-part declaration must not index beyond the storage reserved by its TLM
markers. The bundled decoder has checks before accepting such a tile part and
before writing its index. An owned malformed codestream exercises this boundary.
The component manifest records these checks along with the work-budget patches.

## Tests and remaining work

Run `make test-pdf-jpx` for pinned-source checks, sample-plane comparisons,
container tests, page-image equivalence, color equations and allocation failures.
The two owned assets in `assets/pdf/reference/jpx-*.pdf` have literal pixel
oracles and can be regenerated with `scripts/generate_pdf_jpx_fixtures.py`.
The RGB asset also matches an independent page renderer exactly. The
palette-opacity asset currently differs from that renderer; its expected pixels
come from the declared palette and alpha values, not from copying that output.

A focused check of S2.pdf now renders its first page, including its JPX image and
mask streams. It remains a visual mismatch: mean absolute channel error 5.09197,
maximum 255, with the recorded text, hinting and vector-coverage options enabled.
This is not a full-document qualification or a corpus-wide result.

Remaining work includes e-sRGB/e-sYCC, additional Lab illuminants, nonlinear and
other extended component transforms, wider codestream precision, nonidentity
layer registration, component-registration offsets and broader subsampling
qualification. Chroma-key opacity metadata is parsed, but is not yet exposed as
an embedded PDF soft mask. Repeated images are decoded again and share the page
work limit. Area minification, image-edge coverage and independent interpolation
profiles remain separate rendering gaps. JBIG2 is still unsupported.

## References

- ISO 32000-2:2020, sections 7.4.9, 8.9.5, 11.6.4.3 and 11.6.5.2.
- [Published PDF errata](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html).
- [ITU-T T.800](https://www.itu.int/rec/T-REC-T.800), codestreams and JP2 containers.
- [ITU-T T.801](https://www.itu.int/rec/T-REC-T.801), JPX headers, color specifications and opacity.
- ICC characterisations for [sRGB](https://registry.color.org/rgb-registry/srgb),
  [sYCC](https://registry.color.org/rgb-registry/files/sYCC.pdf) and
  [ROMM RGB](https://registry.color.org/rgb-registry/files/ROMMRGB.pdf).
- [ICC profile specifications](https://www.color.org/icc_specs2.xalter).

The baseline codestream/container review uses the freely available 2002 T.800
and T.801 editions alongside the PDF specification. Later JPEG 2000 extensions
still need a complete applicability review. [RFC 7995](https://www.rfc-editor.org/info/rfc7995/)
describes a constrained PDF publication profile; it does not replace these
image or PDF specifications.
