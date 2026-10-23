# Format conformance

Issue [#45](https://github.com/cupidthecat/cupidimage/issues/45) tracks the full
format review. This is a coverage record, not a claim that every feature of
these specifications is implemented. Unsupported features remain open even
when the current regression suite passes.

## Implemented work in this branch

PNG, TIFF Deflate strips, and PDF Flate streams share one RFC 1950/1951 decoder.
It checks the zlib header and Adler-32, rejects malformed Huffman trees and
length runs, accepts literal-only dynamic blocks, and decodes short final codes
without reading checksum bytes as compressed data. PDF/TIFF no longer retry a
failed zlib stream as raw DEFLATE. Preset dictionaries are not supported by
these container paths.

PNG checks chunk CRCs, header and palette placement, consecutive IDAT chunks,
transparency placement, unknown critical chunks, and IEND. Unknown ancillary
chunks remain skippable. Empty IDAT chunks are valid. The image allocation size
is checked before multiplication.

WebP validates RIFF lengths, chunk padding, duplicate image chunks, and canvas
sizes. Lossy alpha supports raw and lossless-compressed data, all four spatial
filters, and the informational preprocessing flag. VP8 keyframes use separate
header and coefficient readers, including 1/2/4/8 coefficient partitions, the
correct keyframe trees and update probabilities, and U-then-V coefficient
ordering. Intra prediction uses complete macroblock planes and the specified
upper-right boundary samples. The loop filter handles macroblock/subblock
edges, sharpness, mode/reference deltas, and simple versus normal filtering.

VP8 output uses nearest-neighbor chroma upsampling and integer limited-range
BT.601 conversion. It will not necessarily match a viewer's interpolated chroma
output pixel for pixel. The independent regression corpus compares against
FFmpeg-decoded YUV planes converted with the documented output policy. It covers
49 owned images, including varied dimensions, qualities, and partition counts.
This does not establish exhaustive VP8 conformance. VP8 decoding rejects output
over 64 megapixels. Animation and color-profile conversion remain unfinished.

PDF trailer lookup now ignores nested dictionaries and strings when locating
Root. This fixes the linearized empty PDF in the requested PDF.js corpus.
Synthetic cases cover misleading nested keys and a missing real root.

## Coverage and remaining work

| Format/output | Existing coverage | Known gaps or remaining qualification |
| --- | --- | --- |
| ANSI truecolor | RGB escape output, alpha compositing, scaling, write failures | Cross-terminal rendering and the complete alpha/background option matrix |
| Kitty graphics | Transmission, chunking, compression, placement deletion, animation controls | Full protocol feature inventory and live-terminal qualification |
| Sixel | Palette generation, dithering, transparency, emitted stream checks | Device limits, raster aspect behavior, and live-terminal qualification |
| SVG | Static geometry, styles, filters, basic text, sampled animation | Full SVG/CSS semantics, fonts, external resources, and independent visual corpus |
| PNG | Static color types/depths, Adam7, transparency, framing, DEFLATE | APNG frame API, color management, and metadata exposure |
| JPEG | 8-bit baseline/progressive Huffman DCT and existing color modes | Arithmetic/lossless/higher-precision modes, broader sampling and color-profile coverage |
| WebP | VP8/VP8L still images, filtered/compressed alpha, container checks | Animation API/composition, ICC color conversion, broader codec differential tests |
| GIF | Animation, disposal, palettes, interlacing, transparency | Full extension/edge-case corpus and independent composition comparison |
| BMP | Existing Windows/OS2 and compression variants | Header/color-space variants and independent generated-format matrix |
| ICO/CUR | PNG/DIB entries, directory pages, masks, cursor hotspots | Full DIB depth/compression coverage and malformed directory combinations |
| TIFF | Classic/BigTIFF, many photometrics and compression methods | JPEG tiles, planar/tiled YCbCr, predictors/sample formats, complete tag matrix |
| TGA | Existing raw/RLE, origins, palettes, extension metadata | Full format/depth/attribute combinations and independent output comparison |
| PDF | Page geometry, basic text/vector graphics, clipping, forms, image XObjects | Font rendering, transparency, patterns, annotations, filters, object structures, real-document corpus |
| HEIC/HEIF | Todo; no decoder implemented | Container, item properties, codec support, image sequences, color/HDR and metadata |

The 49 files in `assets/webp/reference/` contain real WebP streams generated
from owned synthetic images. Older PNG files mislabeled as WebP have been renamed
and moved to `assets/png/`. See [test assets](test-assets.md) for provenance.

The progressive JPEG audit found an AC-refinement ZRL bug: the decoder counted
sixteen spectral positions instead of sixteen zero coefficients. Existing nonzero
coefficients must consume refinement bits without shortening the zero run. The fix
also rejects runs and newly inserted coefficients that exceed the scan band.
Seventy-two owned grayscale and RGB 4:4:4 JPEGs now compare against independent
libjpeg pixels, including baseline/progressive scans, odd dimensions, noise,
gradients, and three quality levels.

## References

- [ITU-T T.81, Annex G: progressive JPEG](https://www.w3.org/Graphics/JPEG/itu-t81.pdf)
- [PNG Third Edition](https://www.w3.org/TR/png-3/)
- [RFC 1950: zlib](https://www.rfc-editor.org/rfc/rfc1950)
- [RFC 1951: DEFLATE](https://www.rfc-editor.org/rfc/rfc1951)
- [RFC 6386: VP8](https://www.rfc-editor.org/rfc/rfc6386), especially sections 8–15
- [RFC 9649: WebP](https://www.rfc-editor.org/rfc/rfc9649)
- [SVG specification](https://www.w3.org/TR/SVG/)
- [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/)

Additional format references and tests will be recorded as those reviews are
completed. The requested PDF corpora are
[py-pdf/sample-files](https://github.com/py-pdf/sample-files) and
[PDF.js test documents](https://github.com/mozilla/pdf.js/tree/master/test/pdfs).
Check provenance and licenses per fixture before redistribution; a repository's
code license alone is not evidence that every third-party document has that
license.

## Tests

Run the existing suite plus the new conformance tests:

```sh
make -j2 all test
make test-inflate test-png test-webp-container test-webp-pixels
```

The DEFLATE test uses Python's zlib as an independent oracle. The WebP pixel
fixture stores input hashes and expected decoded pixels, so routine tests do
not need Pillow, FFmpeg, downloaded images, or network access. The ordinary
GCC/Clang sanitizer jobs run these tests through `make test`.

The opt-in real PDF corpus uses three immutable, SHA-256-pinned documents
from the requested repositories. It checks all five pages and the blank page's
pixels. It is a load/structure smoke test, not a font or visual equivalence test.
All three selected documents are vendored with their license notices. The PDF.js
samples include font notices for embedded DejaVu subsets. See the asset guide.

```sh
make test-pdf-corpus
# Offline replay:
python3 scripts/test_pdf_corpus.py bin/test_pdf_pages
```

Regenerating the owned WebP corpus requires Pillow with WebP support and FFmpeg
with libvpx enabled:

```sh
python3 scripts/generate_webp_reference.py
```

Encoder version changes can change the compressed bytes. Regeneration is an
explicit maintenance step; normal tests never update their expected output.
