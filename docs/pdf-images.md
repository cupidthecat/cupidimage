# PDF images and DCT samples

Image XObjects and inline images pass decoded samples through PDF color and mask
processing before compositing. A file loading successfully does not establish
complete image fidelity. Color management, interpolation and several compression
formats remain unfinished.

## Sample and filter pipeline

The image path accepts packed 1-, 2- and 4-bit samples, 8-bit samples and
big-endian 16-bit samples in the supported device, CalGray/CalRGB/Lab, supported ICC matrix/tone-curve and indexed color spaces.
See [PDF colors](pdf-colors.md) for conversion order and remaining limits.
Flate, LZW, ASCIIHex, ASCII85, RunLength and CCITT byte filters share the stream
pipeline. DCTDecode can follow those filters instead of requiring a standalone
JPEG stream. Filter arrays may contain up to eight stages.

Filter names, DecodeParms arrays/dictionaries and their controls can be indirect.
Null parameters retain their defaults. Each stage reads only its own controls;
for example, a DCT stage does not interpret a Predictor entry. Unknown keys do
not become controls for a different filter. Flate/LZW prediction follows the
specified Colors, Columns and BitsPerComponent values, including their defaults,
then the image path checks the final sample count against its dimensions.

Image Decode arrays operate on native decoded samples. Color-space conversion,
explicit masks, soft masks and compositing follow. This order matters for CMYK
images and for reversed or extended Decode ranges.

## Image dictionaries and masks

Image and mask dictionaries resolve indirect values before interpreting them.
This includes subtype names, dimensions, bit depth, ImageMask flags, Decode
arrays, color-key Mask arrays and soft-mask Matte arrays. Numeric array members
can also be indirect. Explicit and soft masks may be reached through reference
chains; the resolved stream remains available during sample decoding.

Missing, null and undefined optional entries keep their defaults. A null SMask
does not hide a valid Mask entry. Stencil images may omit BitsPerComponent or
set it to null, in which case it defaults to one. Null ColorSpace entries count
as absent for stencils. Required values and array members still need the right
types: a null element inside Decode is not a number, and cyclic references fail.

Shared dictionary lookup retains the document index and complete stream body.
Shading-pattern stream dictionaries therefore keep their encoded mesh data, while
inline dictionaries keep the same object-resolution context as their parent.

`make test-pdf-image-values` checks direct/indirect equivalence, null defaults,
stencils, explicit and soft masks, Matte, escaped subtype names and required-value
errors. The successful documents also run through allocation-failure injection.
These rules follow ISO 32000-1 sections 7.3.7, 7.3.9 and 8.9, including Table 89.

## DCT color transforms

The private JPEG sample decoder returns components rather than RGBA pixels.
PDF images currently use grayscale, RGB and CMYK results. The sample decoder
also handles two-component frames, but that does not add unsupported PDF color
spaces to the renderer.

For three-component images, ColorTransform 1 converts YCbCr to RGB, while 0
leaves the components unchanged. For four components, 1 converts YCCK to CMYK,
while 0 retains CMYK samples. The option does not transform one- or two-component
samples. Without an encoded marker or parameter, three components default to a
transform and the other component counts do not.

An Adobe APP14 marker takes precedence over the PDF ColorTransform parameter.
Marker 0 leaves components unchanged, marker 1 identifies YCbCr, and marker 2
identifies YCCK. Unsupported component/marker combinations fail. The native
sample path reads through the JPEG end marker before applying its color
transform; a complete MCU without an end marker is not accepted as a complete
DCT stream.

CMYK data is not converted to RGB and then reconstructed. Its four channels
remain available to PDF Decode arrays and the existing unmanaged device-color
conversion. Color-managed CMYK output is still unfinished. JPEG metadata does
not replace the PDF image's Decode array.

## Bounds and remaining work

The decoded sample buffer is limited to 64 MiB. Image dimensions, component count
and bit depth are checked against the JPEG frame before decoding its samples.
Progressive and multi-scan sequential images buffer coefficients before producing
samples. Allocation failures release partial coefficient and output buffers.
Every filter stage retains its own decoded-size bounds. Each component retains the quantization table selected
by its first scan. Table definitions may arrive before the component that needs
them, and a selector may be reused after that component's scans finish. A
conflicting table at a later scan of the same component is rejected.

Coefficient prediction, refinement and dequantization
use checked arithmetic. A dequantized coefficient is limited to an absolute
value of 536,870,911 so the complete fixed-point inverse transform fits in a
signed 64-bit accumulator. Values outside that implementation bound fail rather
than wrapping. Sample clipping happens before narrowing the accumulator result.

The current DCT path supports 8-bit Huffman-coded baseline, extended sequential
and progressive frames, plus arithmetic-coded sequential and progressive frames. Horizontal and vertical sampling factors from one through four are
supported, including 4:1:1 and fractional component ratios. Interleaved scans are
limited to ten data units per MCU; a one-component scan always uses one block per
MCU. Sequential color images may use separate component scans or a mixture of
interleaved and separate scans. Every component must be supplied exactly once in
a sequential frame. Arithmetic scans use the conditioning tables, adaptive
probability states, fixed-probability decisions and restart resets specified in
ISO/IEC 10918-1, Annexes D, F and G. Higher sample precision, hierarchical frames
and other coding modes still need an applicability audit. JPXDecode and JBIG2Decode remain unsupported. These limits are distinct from
image placement, interpolation and color-management work.

Images currently use nearest-neighbor sampling. Scaled-image edge coverage and
rounding can differ from an independent rasterizer even when the decoded samples
agree. A native-resolution codec comparison is not a claim of complete scaled
image fidelity.

## Verification

`make test-pdf-dct` generates baseline and progressive frames, exercises native
component output, and compares filtered PDF images with equivalent raw-sample
images. Cases include marker precedence, filter chains, indirect controls,
Decode arrays and soft masks. The complete PDF cases also run through
allocation-failure injection.

The optional independent pass uses one sample per output pixel and page clipping
to isolate decoding from scaled-image boundary rules:

    python3 scripts/test_pdf_dct.py bin/test_jpeg_samples bin/test_pdf_pages bin/test_pdf_clip_oom --reference

CMYK comparisons retain exact raw-sample equivalents rather than counting an
unmanaged conversion as a color-managed reference match. Existing packed-image,
mask, inline-image and fax suites remain separate regression gates.

`make test-pdf-jpeg-sampling` checks sampling grids, partial edge blocks, restart
intervals, single-component scans and per-component quantization tables. Complete
progressive fixtures include their AC scans so comparisons do not depend on an
independent decoder's progressive-preview smoothing. Selected sample comparisons
can be run with `--reference`; the ordinary tests need no external codec.

The governing rules are in [ISO 32000-1, sections 7.4 and
8.9](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf),
including the ColorTransform precedence in Table 13. The DCT section also refers
to Adobe Technical Note 5116 for its marker conventions. See the
[conformance tracker](format-conformance.md) for the wider PDF work.


`make test-pdf-dct-arithmetic` checks 60 owned pairs of arithmetic- and
Huffman-coded images. Both members use the same pixels, quantization and sampling;
their native samples and rendered PDF images must agree exactly. Cases cover
sequential/progressive scans, grayscale/RGB/CMYK, odd edges, subsampling, restart
markers, filter chains and reversed Decode arrays. Separate-component fixtures
also exercise two-component native output and all four conditioning selectors.
The optional `--reference` pass compares each encoded pair with an independent
decoder. This verifies entropy-code equivalence without attributing shared IDCT
rounding or upsampling differences to arithmetic decoding.

The fixture generator needs ImageMagick, but ordinary tests use the checked-in
fixture data and need no external image codec. See
[ITU-T T.81 / ISO/IEC 10918-1](https://www.w3.org/Graphics/JPEG/itu-t81.pdf)
for the arithmetic coding procedures and probability state table.
`make test-jpeg-math` also checks the standard's complete 256-decision arithmetic
test sequence from K.4.1.
