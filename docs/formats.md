# Format support

These are implemented subsets, not claims of complete standards conformance.
See [the conformance tracker](format-conformance.md) for remaining work and tests.

## SVG

static subset with shapes (rect/circle/ellipse/line/polyline/polygon/path including arc
commands), embedded images (`<image>` with `data:` URIs and local file hrefs), basic text
(`<text>/<tspan>/<textPath>` via built-in bitmap font; includes
`dominant-baseline`/`alignment-baseline` handling and UTF-8 bullet `•` fallback glyph),
presentation + inline styles + basic `<style>` selectors (descendant, child `>`, adjacent
`+`, and general sibling `~` combinators, plus basic `[attr]`/`[attr=value]` matching and
`display`/`visibility`), transforms, viewBox/preserveAspectRatio, linear/radial gradients,
patterns, `hsl()/hsla()` colors, stroke caps/joins/dash, clipPath, luminance and alpha masks
(`mask-type`), basic `<use>` references for primitive shapes and `<symbol>` (with symbol
`preserveAspectRatio` handling and wrapper style attributes), basic SVG markers
(`marker-start`/`marker-mid`/`marker-end`, including `orient=\"auto-start-reverse\"` and
improved join-angle placement), and filter primitives: `feGaussianBlur`, `feOffset`,
`feColorMatrix`, `feComponentTransfer`, `feMorphology`, `feConvolveMatrix`, `feTurbulence`,
`feDisplacementMap`, `feDiffuseLighting`, `feSpecularLighting`, `feTile`, `feImage`
(including `data:` URIs), `feFlood`, `feBlend`, `feComposite`, `feMerge` (including
Source/Background/FillPaint/StrokePaint filter inputs and primitive subregions). Basic
sampled animation support for SMIL (`<set>`, `<animate>`, `<animateTransform>`) and CSS
keyframes (subset: `transform`, `stroke-dashoffset`, `stop-color`) via
`cupidimage_svg_options.animation_time` / CLI `--svg-time`. Supersampled rasterization for
terminal preview. No scripting/DOM, no external web fetches, no custom font loading.

## PNG

non-interlaced and Adam7; color types 0/2/3/4/6 with 1/2/4/8/16-bit depths; palette + tRNS.

## JPEG

baseline/progressive DCT, Huffman-coded, 8-bit precision; grayscale/YCbCr/CMYK/YCCK;
sampling up to 2x2. No arithmetic coding or >2x2 sampling.

## WebP

VP8 lossy keyframes with 1/2/4/8 coefficient partitions; VP8X still images; VP8L lossless
with transforms/color cache; raw and lossless-compressed ALPH with all spatial filters. No
animation or ICC color conversion.

## GIF

single-frame decode and animated GIF composition; palettes, interlacing, transparency;
disposal methods 0-3; loop counts and per-frame delays.

## BMP

Windows/OS/2; BI_RGB/BI_RLE8/BI_RLE4/BI_BITFIELDS/BI_JPEG/BI_PNG; 1/4/8/16/24/32-bit;
top-down/bottom-up; pre-multiplied alpha conversion.

## ICO/CUR

directory enumeration + page-based decode; PNG-compressed and DIB icons at 1/4/8/24/32-bit;
AND mask transparency; CUR hotspots.

## TIFF

Classic + BigTIFF; byte orders II/MM; compression none/LZW/PackBits/Deflate/JPEG/CCITT
RLE/G3/G4; photometric bilevel/grayscale(+alpha)/RGB/RGBA/palette/CMYK/YCbCr/Lab;
strips/tiles; planar 1/2; predictor=2; orientation 1-8; multi-page IFDs (SubIFD pages via
page APIs).

## TGA

types 0-3/9-11; uncompressed/RLE; 8/15/16/24/32-bit; all origins;
color-mapped/RGB/grayscale; TGA 2.0 metadata + thumbnail + gamma/color correction.

## PDF

basic text, raw, FlateDecode, ASCIIHexDecode, ASCII85Decode, RunLengthDecode, and LZWDecode page streams, gray/RGB/CMYK paint, line and cubic Bezier paths,
nonzero/even-odd fills, and path clipping. Text uses built-in bitmap glyphs. Image XObjects
support 1/2/4/8/16-bit DeviceRGB, DeviceGray, and DeviceCMYK samples in raw or
FlateDecode streams. Baseline and progressive grayscale/YCbCr JPEG images use
DCTDecode. Images also support TIFF/PNG predictors, Decode
arrays, and affine placement. Form XObjects render nested content with local resources,
transforms, and bounding-box clipping. Previews use inherited MediaBox/CropBox rectangles,
nonzero page origins, and clockwise rotation in multiples of 90 degrees. Complex fonts, text
clipping modes, transparency groups, blend modes, patterns, and annotations are unsupported.

## PDF details and limits

PDF pages render at 96 DPI. CropBox is intersected with MediaBox, and page
rotation applies to both text and graphics. Direct and indirect page boxes and
rotation values are supported. Invalid boxes, cyclic parent chains, and previews
larger than 64 megapixels are rejected. The renderer still trims whitespace from
the resulting preview.

Run the generated PDF page regressions with Python 3:

    make test-pdf-pages

PDF image XObjects use the page's Resources dictionary, including inherited and
indirect resource dictionaries. Images use nearest-neighbor sampling. Flate
images accept predictor 1 (none), 2 (TIFF), and 10-15 (PNG), with 1/2/4/8/16-bit samples.
Predictor Colors and Columns must match the image when prediction is enabled.
The decoded sample data is limited to 64 MiB per image.

JPEG images support DCTDecode as a name or a single-filter array, including
ColorTransform 1 and Decode array inversion. Their frame dimensions, bit depth,
and component count must match the image dictionary. FlateDecode also accepts a
single-filter array. Non-JPEG images also accept ASCIIHexDecode, ASCII85Decode,
RunLengthDecode, LZWDecode, and chains of these byte filters with FlateDecode.
LZW supports both EarlyChange values. Flate/LZW stages accept direct predictor
dictionaries, including packed and 16-bit TIFF/PNG prediction. Raw RGB JPEG images, ColorTransform 0, JPX,
inline images
are outside the supported subset.
Drawing an invalid or unsupported XObject returns a load error.

Type 1 Form XObjects accept raw streams and the same byte filters as page content. Their Matrix transforms
the content, and BBox clips text, paths, and images, including nested forms.
Forms use their own Resources dictionary when present, or the caller's resources
otherwise. Drawing a form preserves the caller's graphics state and current path.
Built-in text glyphs follow the current transformation matrix. General text
matrices and complex font rendering remain unsupported.

Forms are limited to 16 nesting levels, 4096 invocations, and 64 MiB of combined
decoded form content per page, counting repeated invocations. Recursive forms, transparency
groups, external-reference forms, filter chains, and form DecodeParms are rejected.

Run the generated form checks with Python 3:

    make test-pdf-forms

Path clipping supports `W` (nonzero winding) and `W*` (even-odd). The clip takes
effect after the path's painting operator, including `n`, and intersects the
current clip. It applies to paths, text, images, and nested forms. `q` and `Q`
save and restore the clip. Path coordinates retain their placement when the
transformation matrix changes. Clipping samples pixel centers without antialiasing.

Each page allows 4096 clip operations, 4096 segments per clipping path, 32 saved
graphics states, and 64 MiB of live clipping masks. A shared budget limits clipping
to 268,435,456 pixel-edge tests per page. Documents exceeding these limits return
a load error.

Run the clipping and allocation-failure checks with Python 3 and a C compiler:

    make test-pdf-clipping test-pdf-clip-oom

PDF colors support DeviceGray, DeviceRGB, and DeviceCMYK through `cs`/`CS` and
`sc`/`SC`/`scn`/`SCN`, as well as `g`/`G`, `rg`/`RG`, and `k`/`K`. Fill and
stroke color spaces are independent and follow `q`/`Q` state restoration.
Selecting a color space resets its color to black.

Named ColorSpace resources can alias device spaces, including inherited and
indirect dictionaries and indirect name values. Forms use their own resources
when present and otherwise inherit the caller's resources. Alias resolution is
bounded to reject cycles. Unsupported spaces, including patterns, calibrated
colors, indexed colors, and ICC profiles, return a load error. Invalid color
operands also return an error. CMYK uses the built-in RGB conversion without
color profiles.

Run the generated color and resource checks with:

    make test-pdf-colors

Run the generated image checks with Python 3:

    make test-pdf-images


Page and form content accept FlateDecode (`Fl`), ASCIIHexDecode (`AHx`),
ASCII85Decode (`A85`), RunLengthDecode (`RL`), and LZWDecode (`LZW`). Filter arrays decode in order,
with at most eight stages and 64 MiB of output per stage. Unknown filters,
truncated encodings, and invalid ASCII85 tuples are rejected. DecodeParms may be
null, a direct dictionary for one filter, or an array aligned with the filter
array. Direct Flate/LZW dictionaries accept Predictor, Colors, Columns,
BitsPerComponent, and EarlyChange. Indirect DecodeParms objects remain supported
only by the single-Flate image path. Combined page content is limited to 64 MiB.

## Packed image samples and masks

Raw and Flate image streams support 1-, 2-, 4-, 8-, and 16-bit components, including
odd-width row padding. TIFF horizontal differencing operates on whole components;
PNG predictors operate on the correct packed byte stride. Sixteen-bit samples
retain their precision through prediction and Decode mapping before conversion to
8-bit RGBA.

Stencil ImageMask images use the current fill color, with normal or inverted
Decode polarity. Color-key Mask arrays make matching source samples transparent.
Explicit Mask image streams support one-bit stencils. DeviceGray SMask images
support 1/2/4/8/16-bit samples, Decode arrays, raw/Flate streams, and supported
predictors. Masks can have different dimensions from the parent image and are
sampled independently over its unit square. Image sampling remains nearest-neighbor;
Interpolate is not implemented.

SMask takes precedence over Mask. A Matte array removes preblending in the parent
color space before color conversion. Matte masks must have the same dimensions as
the parent. Nested masks, malformed masks, and unsupported mask codecs return an
error rather than drawing an opaque substitute. Graphics-state soft masks,
transparency groups, and blend modes are not implemented.

Run `make test-pdf-masks` for exact-pixel and malformed-input coverage. The optional
`python3 scripts/test_pdf_masks.py bin/test_pdf_pages --reference` also compares
successful fixtures against an installed independent rasterizer, with vector
antialiasing disabled to match pixel-center clipping.

DeviceCMYK images
use the same unmanaged conversion as device-color paint; DCT-compressed CMYK
images and color-managed conversion remain outside this subset.

## Deferred formats

HEIC/HEIF is planned for later. It is not implemented or included in supported-format claims.

## Indexed PDF images

Image XObjects accept Indexed color spaces with DeviceGray, DeviceRGB, or
DeviceCMYK palettes. Color-space arrays and palette strings may be indirect;
palettes can also be filtered streams. Literal and hexadecimal palette strings
follow PDF escaping and whitespace rules. Palette lengths and index bounds are
validated. Decode mappings round indices and clamp them to the palette range.
Color-key masks, explicit masks, and soft masks work with indexed images.
Indexed images with Matte preblending remain unsupported and return an error.

`make test-pdf-indexed` checks packed indices, palette sources, Decode mappings,
masks, and malformed or cyclic references. Full 16-bit indices and tolerated
indirect-reference chains use analytic expectations because the installed
comparison renderers do not preserve those inputs. DeviceCMYK expectations use
the library's unmanaged conversion.
