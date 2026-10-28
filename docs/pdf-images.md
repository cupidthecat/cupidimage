# PDF images and DCT samples

Image XObjects and inline images pass decoded samples through PDF color and mask
processing before compositing. A file loading successfully does not establish
complete image fidelity. Color management, interpolation and several compression
formats remain unfinished.

## Sample and filter pipeline

The image path accepts packed 1-, 2- and 4-bit samples, 8-bit samples and
big-endian 16-bit samples in the supported device and indexed color spaces.
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
Progressive coefficients need additional working memory; allocation failures
release partial coefficient and output buffers. Every filter stage retains its
own decoded-size bounds. Coefficient prediction, refinement and dequantization
use checked arithmetic. A dequantized coefficient is limited to an absolute
value of 536,870,911 so the complete fixed-point inverse transform fits in a
signed 64-bit accumulator. Values outside that implementation bound fail rather
than wrapping. Sample clipping happens before narrowing the accumulator result.

The current DCT path supports 8-bit, Huffman-coded baseline and progressive
frames. Sampling factors above two and noninterleaved sequential color scans are
not yet supported. Other DCT coding modes need an applicability and implementation
audit. JPXDecode and JBIG2Decode remain unsupported. These limits are distinct from
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

The governing rules are in [ISO 32000-1, sections 7.4 and
8.9](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf),
including the ColorTransform precedence in Table 13. The DCT section also refers
to Adobe Technical Note 5116 for its marker conventions. See the
[conformance tracker](format-conformance.md) for the wider PDF work.
